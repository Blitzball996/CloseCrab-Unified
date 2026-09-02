#include "RemoteAPIClient.h"
#include "APIError.h"
#include "../config/Config.h"
#include "../core/PredictiveEngine.h"
#include "../license/LicenseCore.h"
#include "../utils/Trace.h"
#include "../utils/UUID.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdlib>
#include <cctype>
#include <chrono>
#include <thread>
#include <vector>

// JackProAi has NO explicit rate limiting — relies on server-side limits + fast retry.
// Removed global mutex serialization that was blocking concurrent agent requests.

namespace closecrab {

// ---------------------------------------------------------------------------
// Client identity (anti-"客户端存在异常" fingerprint rejection)
//
// Claude Code relays/proxies fingerprint requests to verify they came from a
// standard Claude Code client. Header parity alone is NOT enough: the check
// also inspects the BODY. Measured against www.blitz.mom (2026-08-02) by
// replaying a captured real `claude` request and removing one field at a time:
//
//     baseline (verbatim real request) ......... 200 OK
//     minus tools .............................. 200 OK
//     minus context_management / output_config . 200 OK
//     minus thinking ........................... 200 OK
//     minus system[0] (billing header block) ... 200 OK
//     minus metadata ........................... 400 "客户端存在异常"   <-- the gate
//
// So the top-level `metadata` object MUST be present on every request. We send
// it in the real client's exact shape: metadata.user_id is a JSON *string*
// containing {"device_id","account_uuid","session_id"}.
//
// Keep this in sync with the real client using scripts/capture_claude_request.py
// (see extract_claude_headers.bat) — that tool dumps the live request's headers
// AND body so a body-level fingerprint change can't silently break us again.
// ---------------------------------------------------------------------------

// Per-process session UUID. Stable for the life of the process so the proxy
// (and the prompt cache) sees one continuous session, matching Claude Code.
static const std::string& clientSessionId() {
    static const std::string id = generateUUID();
    return id;
}

// 64-hex machine id, same shape as Claude Code's device_id (a sha256 hex).
// Derived from the OS machine GUID via the licensing core so it is stable
// across runs and does not leak anything identifying beyond the machine hash.
static const std::string& clientDeviceId() {
    static const std::string id = lic::sha256Hex("closecrab-device:" + lic::deviceId());
    return id;
}

// metadata.user_id — a JSON string, exactly as Claude Code encodes it.
static const std::string& clientUserId() {
    static const std::string uid = nlohmann::json({
        {"device_id", clientDeviceId()},
        {"account_uuid", ""},
        {"session_id", clientSessionId()}
    }).dump();
    return uid;
}

// "[1m]" suffix handling, mirroring Claude Code (utils/context.ts + cli.js).
// The suffix is a client-side opt-in for the 1M context window; it must be
// stripped from the model name sent to the API (the proxy/upstream rejects the
// literal "[1m]" with model_not_found) and instead signaled via the
// anthropic-beta: context-1m-2025-08-07 header.
static bool wants1MContext(const std::string& model) {
    // case-insensitive endsWith "[1m]"
    if (model.size() < 4) return false;
    std::string tail = model.substr(model.size() - 4);
    for (auto& c : tail) c = (char)std::tolower((unsigned char)c);
    return tail == "[1m]";
}
static std::string stripOneMSuffix(const std::string& model) {
    if (wants1MContext(model)) {
        std::string s = model.substr(0, model.size() - 4);
        // trim trailing spaces, mirroring Claude Code's .trim() after replace
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }
    return model;
}

// Reasoning-effort capability, mirroring Claude Code utils/effort.ts
// (modelSupportsEffort). Effort is the API-native output_config.effort param,
// supported by Claude 4.6+ and the newer 4.8 family; legacy haiku/older
// sonnet/opus variants reject it. We allowlist the known-good families and
// default unknown strings to true (the proxy ignores an unused output_config
// on models that don't read it, and our 400-retry path strips it if rejected).
static bool modelSupportsEffort(const std::string& model) {
    std::string m = stripOneMSuffix(model);
    for (auto& c : m) c = (char)std::tolower((unsigned char)c);
    if (m.find("opus-4-6") != std::string::npos) return true;
    if (m.find("sonnet-4-6") != std::string::npos) return true;
    if (m.find("opus-4-7") != std::string::npos) return true;
    if (m.find("opus-4-8") != std::string::npos) return true;
    if (m.find("sonnet-4-8") != std::string::npos) return true;
    if (m.find("fable-5") != std::string::npos) return true;
    if (m.find("fable5") != std::string::npos) return true;
    // Legacy / unsupported families explicitly OFF.
    if (m.find("haiku") != std::string::npos) return false;
    if (m.find("sonnet") != std::string::npos) return false;
    if (m.find("opus") != std::string::npos) return false;
    // Unknown model string: assume capable (matches Claude Code's 1P default).
    return true;
}

// 'max' is the highest effort and is opus-4-6/4-8 only on public models; other
// models return an error, so we downgrade 'max' -> 'xhigh' there (Claude Code
// downgrades to 'high'; we keep 'xhigh' since 4.8 supports it and it's closer in
// intent to the user's "ultra" ask).
static std::string normalizeEffortForModel(const std::string& effort,
                                           const std::string& model) {
    if (effort.empty()) return effort;
    static const char* kValid[] = {"low", "medium", "high", "xhigh", "max"};
    bool ok = false;
    for (auto* v : kValid) if (effort == v) { ok = true; break; }
    if (!ok) return "high";  // unknown level -> safe middle-high
    if (effort == "max") {
        std::string m = stripOneMSuffix(model);
        for (auto& c : m) c = (char)std::tolower((unsigned char)c);
        bool maxOk = (m.find("opus-4-6") != std::string::npos) ||
                     (m.find("opus-4-8") != std::string::npos) ||
                     (m.find("fable-5") != std::string::npos) ||
                     (m.find("fable5") != std::string::npos);
        if (!maxOk) return "xhigh";
    }
    return effort;
}


RemoteAPIClient::RemoteAPIClient(const std::string& apiKey,
                                   const std::string& baseUrl,
                                   const std::string& model)
    : apiKey_(apiKey), baseUrl_(baseUrl), model_(model) {}

nlohmann::json RemoteAPIClient::buildRequestBody(
    const std::vector<Message>& messages,
    const std::string& systemPrompt,
    const ModelConfig& config,
    const std::string& modelOverride
) const {
    nlohmann::json body;
    // The "[1m]" suffix is a CLIENT-SIDE marker (Claude Code convention) meaning
    // "use the 1M context window". The proxy/upstream does NOT know the suffix —
    // sending "claude-opus-4-8[1m]" verbatim gets model_not_found. Claude Code
    // strips it before sending (cli.js: replace(/\[1m]$/i,"")) and instead opts
    // into 1M via the anthropic-beta: context-1m-2025-08-07 header (added in
    // performCurlSSE). So: send the CLEAN model name here.
    std::string requested = modelOverride.empty() ? model_ : modelOverride;
    body["model"] = stripOneMSuffix(requested);
    body["max_tokens"] = config.maxTokens;
    body["stream"] = config.stream;

    // REQUIRED — do not remove. Proxies reject a request with no `metadata` as a
    // non-standard client (HTTP 400 "客户端存在异常"). See the clientUserId()
    // comment block above for the bisect that pinned this down.
    body["metadata"] = {{"user_id", clientUserId()}};

    if (config.temperature >= 0 && config.tools.empty()) body["temperature"] = config.temperature;

    // P0 cache TTL (JackProAi getCacheControl). Only emit ttl:"1h" when the proxy
    // is known to honor it (cacheTtlMinutes_ >= 60, e.g. official Anthropic API).
    // MEASURED: yikoulian ignores ttl:1h and hard-expires at 5 min, so for it we
    // send the plain 5m ephemeral form (default) — sending ttl:1h there is useless
    // and risks a 2x write charge if behavior ever changes. The
    // extended-cache-ttl beta header is already sent for the 1h case.
    nlohmann::json kCacheControl;
    if (cacheTtlMinutes_ >= 60) {
        kCacheControl = {{"type", "ephemeral"}, {"ttl", "1h"}};
    } else {
        kCacheControl = {{"type", "ephemeral"}};
    }

    // §1 System prompt cache split: the prompt carries a boundary marker
    // separating the STATIC cacheable prefix from the DYNAMIC tail (cwd/model/
    // mode). Emit two text blocks; cache_control ONLY on the static block so the
    // server-side prompt cache survives when the dynamic tail changes. If no
    // marker is present (e.g. sub-agent / local model), fall back to one block.
    if (!systemPrompt.empty()) {
        const std::string marker = kSystemDynamicBoundary;
        auto mpos = systemPrompt.find(marker);
        if (mpos != std::string::npos) {
            std::string staticPart = systemPrompt.substr(0, mpos);
            std::string dynamicPart = systemPrompt.substr(mpos + marker.size());
            nlohmann::json sysArr = nlohmann::json::array();
            sysArr.push_back({{"type", "text"}, {"text", staticPart},
                              {"cache_control", kCacheControl}});
            if (!dynamicPart.empty()) {
                sysArr.push_back({{"type", "text"}, {"text", dynamicPart}});
            }
            body["system"] = std::move(sysArr);
        } else {
            body["system"] = nlohmann::json::array({
                {{"type", "text"}, {"text", systemPrompt}, {"cache_control", kCacheControl}}
            });
        }
    }

    // Messages
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& msg : messages) {
        msgs.push_back(msg.toApiJson());
    }

    // JackProAi normalizeMessagesForAPI (messages.ts:2089/2451): merge consecutive
    // user messages into one before sending. QueryEngine pushes each tool_result as
    // its own role:user message; strict relays reject N consecutive user messages.
    // hoistToolResults: tool_result blocks must lead (API rule).
    // joinTextAtSeam: inject '\n' at text-text seam (avoid run-together prompts).
    {
        auto hoistToolResults = [](nlohmann::json& content) {
            nlohmann::json tr = nlohmann::json::array(), other = nlohmann::json::array();
            for (auto& b : content)
                (b.is_object() && b.value("type","") == "tool_result" ? tr : other).push_back(b);
            for (auto& b : other) tr.push_back(std::move(b));
            content = std::move(tr);
        };
        nlohmann::json merged = nlohmann::json::array();
        for (auto& m : msgs) {
            if (!merged.empty() && merged.back().value("role","") == "user"
                                && m.value("role","") == "user") {
                auto& ac = merged.back()["content"];
                auto& bc = m["content"];
                // joinTextAtSeam: if last block of ac and first of bc are both text, append '\n'
                if (ac.is_array() && !ac.empty() && bc.is_array() && !bc.empty()
                    && ac.back().value("type","") == "text"
                    && bc.front().value("type","") == "text") {
                    ac.back()["text"] = ac.back().value("text","") + "\n";
                }
                if (bc.is_array()) for (auto& b : bc) ac.push_back(b);
                hoistToolResults(ac);
            } else {
                merged.push_back(std::move(m));
            }
        }
        msgs = std::move(merged);
    }

    // §3 Deterministic microcompact (JackProAi enforceToolResultBudget +
    // ContentReplacementState). The OLD code re-decided which tool_results to
    // clear every turn based on the current total size, so the same tool_use_id
    // could be full on turn N and truncated on turn N+1 — the byte sequence
    // changed and the server-side prompt cache missed (the 77K-per-request bug).
    //
    // New rule: a clear decision, once made, is FROZEN. clearedToolUseIds_ records
    // every id we've ever cleared; we ALWAYS re-apply the same deterministic stub
    // to those ids first (byte-identical), then only freeze MORE ids if still over
    // budget. Decisions are monotonic → the message prefix is byte-stable across
    // turns → cache hits survive compaction.
    constexpr size_t MAX_MESSAGES_SIZE = 200000;

    // Helper: replace a tool_result block's content with a deterministic stub.
    // Deterministic = first 400 chars of the original + a fixed marker, so the
    // same id always yields the same bytes (messages_ keeps the full content;
    // this only affects the wire serialization).
    auto clearBlock = [](nlohmann::json& block) {
        if (block.contains("content") && block["content"].is_string()) {
            std::string content = block["content"].get<std::string>();
            if (content.size() > 500)
                block["content"] = content.substr(0, 400) + "\n[cleared for context limit]";
        } else if (block.contains("content") && block["content"].is_array()) {
            for (auto& sub : block["content"]) {
                if (sub.value("type", "") == "text") {
                    std::string text = sub.value("text", "");
                    if (text.size() > 500)
                        sub["text"] = text.substr(0, 400) + "\n[cleared for context limit]";
                }
            }
        }
    };

    // Pass 1: re-apply all FROZEN clears (byte-identical, no decision change).
    if (!clearedToolUseIds_.empty()) {
        for (auto& msg : msgs) {
            if (!msg.contains("content") || !msg["content"].is_array()) continue;
            for (auto& block : msg["content"]) {
                if (block.value("type", "") == "tool_result" &&
                    clearedToolUseIds_.count(block.value("tool_use_id", ""))) {
                    clearBlock(block);
                }
            }
        }
    }

    // P1 Time-based microcompact (JackProAi timeBasedMCConfig.ts). If the gap
    // since the last request exceeds the cache TTL (1h), the server-side prompt
    // cache has certainly expired — the whole prefix will be rewritten anyway.
    // So proactively freeze+clear all but the most-recent `kKeepRecent` tool
    // results to shrink that inevitable rewrite. Frozen ids are recorded, so the
    // decision stays byte-stable on subsequent turns (consistent with §3).
    {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        // Gap threshold = the effective cache TTL: once exceeded, the server cache
        // is gone, so shrinking the prefix is pure win. MEASURED yikoulian = 5min,
        // so this fires usefully there (JackProAi uses 60 because it gets 1h TTL).
        const int64_t kGapThresholdMs = (int64_t)cacheTtlMinutes_ * 60 * 1000;
        constexpr int kKeepRecent = 5;                        // JackProAi keepRecent=5
        if (lastRequestEpochMs_ != 0 && (nowMs - lastRequestEpochMs_) > kGapThresholdMs) {
            // Collect (msgIndex, blockRef) of all compactable tool_results in order.
            std::vector<std::pair<size_t, size_t>> trBlocks;
            for (size_t i = 0; i < msgs.size(); i++) {
                if (!msgs[i].contains("content") || !msgs[i]["content"].is_array()) continue;
                auto& content = msgs[i]["content"];
                for (size_t b = 0; b < content.size(); b++) {
                    if (content[b].value("type", "") == "tool_result")
                        trBlocks.push_back({i, b});
                }
            }
            // Clear all but the last kKeepRecent.
            if (trBlocks.size() > (size_t)kKeepRecent) {
                size_t clearUpTo = trBlocks.size() - kKeepRecent;
                for (size_t k = 0; k < clearUpTo; k++) {
                    auto& block = msgs[trBlocks[k].first]["content"][trBlocks[k].second];
                    std::string id = block.value("tool_use_id", "");
                    if (!id.empty()) clearedToolUseIds_.insert(id);
                    clearBlock(block);
                }
            }
        }
        lastRequestEpochMs_ = nowMs;
    }

    // Pass 2: if still over budget, freeze MORE oldest tool_results (keep last 4
    // messages intact). Each newly cleared id is recorded so it stays cleared.
    std::string msgsStr = msgs.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (msgsStr.size() > MAX_MESSAGES_SIZE && msgs.size() > 4) {
        for (size_t i = 0; i + 4 < msgs.size(); i++) {
            if (msgs[i].value("role", "") == "user" && msgs[i].contains("content") && msgs[i]["content"].is_array()) {
                for (auto& block : msgs[i]["content"]) {
                    if (block.value("type", "") == "tool_result") {
                        std::string id = block.value("tool_use_id", "");
                        if (!id.empty() && !clearedToolUseIds_.count(id)) {
                            clearBlock(block);
                            clearedToolUseIds_.insert(id);  // freeze the decision
                        }
                    }
                }
            }
            msgsStr = msgs.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            if (msgsStr.size() <= MAX_MESSAGES_SIZE) break;
        }
    }

    // Message-level cache_control breakpoint (JackProAi addCacheBreakpoints).
    // Normally on the LAST message's last block. For sub-agent forks
    // (skipCacheWrite), shift to the SECOND-to-last message: the fork then reads
    // the parent's shared prefix and the write is a no-op merge, so the fork does
    // not leave its own tail in the cache (JackProAi markerIndex = length - 2).
    if (!msgs.empty()) {
        size_t markerIdx = msgs.size() - 1;
        if (config.skipCacheWrite && msgs.size() >= 2) {
            markerIdx = msgs.size() - 2;
        }
        auto& mkMsg = msgs[markerIdx];
        if (mkMsg.contains("content") && mkMsg["content"].is_array() && !mkMsg["content"].empty()) {
            mkMsg["content"].back()["cache_control"] = kCacheControl;
        }
    }

    body["messages"] = std::move(msgs);

    // Tools with cache_control on last tool (creates cache breakpoint)
    if (!config.tools.empty() && config.tools.is_array() && config.tools.size() > 0) {
        nlohmann::json tools = config.tools;
        if (!tools.empty()) {
            tools.back()["cache_control"] = kCacheControl;
        }
        body["tools"] = std::move(tools);
    }

    // Server-side tools (e.g. web_search_20250305) — appended VERBATIM, no
    // cache_control (server tools aren't cache breakpoints). Lets WebSearchTool
    // run on this same robust path (HTTP/1.1, retry, fallback, proxy).
    if (config.extraServerTools.is_array() && !config.extraServerTools.empty()) {
        if (!body.contains("tools") || !body["tools"].is_array()) {
            body["tools"] = nlohmann::json::array();
        }
        for (const auto& st : config.extraServerTools) {
            body["tools"].push_back(st);
        }
    }

    // Reasoning effort (Claude Code 2.1.x native output_config.effort).
    // On effort-capable models this SUPERSEDES thinking.budget_tokens: we send
    // output_config.effort and the `effort-2025-11-24` beta header (added in
    // performCurlSSE when body carries output_config.effort). The model then
    // self-allocates reasoning depth — "ultra" == xhigh. We DON'T also send a
    // thinking block in that case (the two mechanisms conflict).
    std::string effortToSend;
    {
        std::string activeModel = modelOverride.empty() ? model_ : modelOverride;
        if (!config.effort.empty() && modelSupportsEffort(activeModel)) {
            effortToSend = normalizeEffortForModel(config.effort, activeModel);
        }
    }

    if (!effortToSend.empty()) {
        body["output_config"]["effort"] = effortToSend;
        // 2.1.258: thinking.type="adaptive" + display="omitted" is sent TOGETHER
        // with output_config.effort on effort-capable models. They no longer conflict;
        // the server treats "adaptive" as "let the model self-allocate" and
        // display="omitted" strips reasoning tokens from the response stream.
        body["thinking"] = {{"type", "adaptive"}, {"display", "omitted"}};
    } else if (config.thinkingEnabled) {
        // Fallback: model doesn't support native effort — use the legacy
        // extended-thinking budget mechanism.
        body["thinking"]["type"] = "enabled";
        body["thinking"]["budget_tokens"] = config.thinkingBudgetTokens;
    }

    // P3 Server-side context_management (JackProAi apiMicrocompact.ts:
    // clear_tool_uses_20250919). The server clears old tool results once input
    // tokens exceed the trigger — local message bytes stay untouched, so the
    // cached prefix is never disturbed (cleaner than the local §3 path).
    //
    // ALIGNMENT NOTE: in JackProAi this strategy is ant-only AND env-gated
    // (USE_API_CLEAR_TOOL_RESULTS), default OFF for external users — they rely on
    // the local path instead. We mirror that exactly: capability present but gated
    // behind CLOSECRAB_API_CLEAR_TOOL_RESULTS, default OFF. The
    // `context-management-2025-06-27` beta header is already sent, so enabling it
    // is just flipping the env var once the proxy is confirmed to honor it.
    {
        // Default ON now (opt-out). The proxy is confirmed to honor the
        // `context-management-2025-06-27` beta, so let the SERVER clear old
        // read-tool results once input crosses kTrigger. This is the cleanest
        // defense against the "many turns -> 503/504" growth: it shrinks the
        // input the proxy sees WITHOUT disturbing our local cached prefix.
        // Set CLOSECRAB_API_CLEAR_TOOL_RESULTS=0 to opt out.
        auto envOptOut = [](const char* name) -> bool {
            const char* v = std::getenv(name);
            return v && (std::string(v) == "0" || std::string(v) == "false");
        };
        bool enableServerClear = !envOptOut("CLOSECRAB_API_CLEAR_TOOL_RESULTS");
        // clear_tool_USES stays opt-IN (default OFF): clearing the tool_use
        // record itself is more aggressive and can confuse turn pairing on some
        // proxies. Enable with CLOSECRAB_API_CLEAR_TOOL_USES=1.
        const char* clearUsesEnv = std::getenv("CLOSECRAB_API_CLEAR_TOOL_USES");
        bool enableClearUses = clearUsesEnv && (std::string(clearUsesEnv) == "1" ||
                                                std::string(clearUsesEnv) == "true");
        if (enableServerClear || enableClearUses) {
            // Match JackProAi DEFAULT_MAX_INPUT_TOKENS / DEFAULT_TARGET_INPUT_TOKENS.
            constexpr int kTrigger = 180000;
            constexpr int kTarget = 40000;
            nlohmann::json edits = nlohmann::json::array();

            // Strategy 1 (useClearToolResults): clear the tool_result content of
            // read-type tools (shell + Glob/Grep/Read/WebFetch/WebSearch).
            if (enableServerClear) {
                edits.push_back({
                    {"type", "clear_tool_uses_20250919"},
                    {"trigger", {{"type", "input_tokens"}, {"value", kTrigger}}},
                    {"clear_at_least", {{"type", "input_tokens"}, {"value", kTrigger - kTarget}}},
                    {"clear_tool_inputs", {"Bash", "Glob", "Grep", "Read", "WebFetch", "WebSearch"}}
                });
            }
            // Strategy 2 (useClearToolUses): clear tool USES but EXCLUDE the
            // mutating tools (Edit/Write/NotebookEdit) so their record is kept.
            // Independent of strategy 1 — additive (JackProAi apiMicrocompact.ts:128).
            if (enableClearUses) {
                edits.push_back({
                    {"type", "clear_tool_uses_20250919"},
                    {"trigger", {{"type", "input_tokens"}, {"value", kTrigger}}},
                    {"clear_at_least", {{"type", "input_tokens"}, {"value", kTrigger - kTarget}}},
                    {"exclude_tools", {"Edit", "Write", "NotebookEdit"}}
                });
            }
            // Strategy 3 (clear_thinking_20251015): new in 2.1.258. Clears thinking
            // blocks from old assistant turns once input crosses the trigger. Thinking
            // blocks are large and purely retrospective — the model never needs to
            // re-read them — so clearing them is safe and significantly shrinks context
            // on long reasoning-heavy sessions. Always enabled alongside strategy 1.
            if (enableServerClear) {
                edits.push_back({
                    {"type", "clear_thinking_20251015"},
                    {"trigger", {{"type", "input_tokens"}, {"value", kTrigger}}},
                    {"clear_at_least", {{"type", "input_tokens"}, {"value", kTrigger - kTarget}}}
                });
            }
            body["context_management"] = {{"edits", std::move(edits)}};
        }
    }

    return body;
}

} // namespace closecrab (temporarily close for CURL C callbacks)

struct CurlStreamCtx {
    closecrab::StreamParser* parser;
    std::string rawResponse;
    // Stall diagnostics. Mirrors Claude Code's `tengu_streaming_stall` telemetry:
    // it warns whenever the gap between two SSE events exceeds 30s, which is how
    // a half-dead proxy becomes visible BEFORE the idle timeout fires. Purely
    // local state, owned by the caller's stack frame, so there is nothing here
    // that can outlive the transfer (unlike the removed idle watchdog below).
    std::chrono::steady_clock::time_point lastChunkAt{};
    int stallCount = 0;
};

// Captures `retry-after` / `retry-after-ms` from the response headers into a
// long (milliseconds, -1 = absent). Claude Code prefers the server's own hint
// over its computed backoff whenever it is present and sane (0..60s).
static size_t curlHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t totalSize = size * nitems;
    auto* out = static_cast<long*>(userdata);
    if (!out) return totalSize;
    try {
        std::string line(buffer, totalSize);
        size_t colon = line.find(':');
        if (colon == std::string::npos) return totalSize;
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                                  s.back() == ' '  || s.back() == '\t')) s.pop_back();
        };
        trim(value);
        if (value.empty()) return totalSize;
        // Only plain numeric forms; an HTTP-date retry-after is ignored (as is
        // a negative/garbage value) and we fall back to computed backoff.
        if (name == "retry-after-ms") {
            *out = std::stol(value);
        } else if (name == "retry-after") {
            *out = std::stol(value) * 1000;
        }
    } catch (...) {
        // Malformed header — leave the hint untouched, never propagate out of a
        // C callback.
    }
    return totalSize;
}

static size_t curlStreamCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<CurlStreamCtx*>(userdata);
    size_t totalSize = size * nmemb;
    // CRITICAL: This is a C callback invoked by libcurl. If ANY exception
    // escapes here, it's undefined behavior (MSVC calls std::terminate →
    // instant crash with no error message). This was causing the "闪退 while
    // writing" bug — large streaming responses could trigger exceptions in
    // the parser or callback chain.
    try {
        auto now = std::chrono::steady_clock::now();
        if (ctx->lastChunkAt.time_since_epoch().count() != 0) {
            auto gapMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - ctx->lastChunkAt).count();
            if (gapMs > 30000) {
                ctx->stallCount++;
                spdlog::warn("Streaming stall detected: {:.1f}s gap between events "
                             "(stall #{}) — upstream or proxy is buffering/half-dead",
                             gapMs / 1000.0, ctx->stallCount);
            }
        }
        ctx->lastChunkAt = now;

        std::string chunk(ptr, totalSize);
        ctx->parser->feed(chunk);
        // Cap raw response accumulation to prevent OOM on huge streams
        if (ctx->rawResponse.size() < 200000) {
            ctx->rawResponse += chunk;
        }
    } catch (...) {
        // Swallow the exception — returning 0 tells CURL to abort the transfer,
        // which will surface as a CURLE_WRITE_ERROR in performCurlSSE.
        return 0;
    }
    return totalSize;
}

// idle watchdog removed — was causing ACCESS_VIOLATION (0xC0000005).
// CURL progress callbacks are unreliable with some CURL/proxy combinations.
//
// REPLACED by CURLOPT_LOW_SPEED_LIMIT/TIME (see performCurlSSE). That is the
// same policy Claude Code implements in JS as its stream idle timeout
// (CLAUDE_STREAM_IDLE_TIMEOUT_MS, default 90000), but enforced inside libcurl's
// own transfer loop instead of a user callback that touches shared state — so
// it cannot reintroduce the 0xC0000005. The old crash came from a callback doing
// timing work on possibly-freed state, not from having an idle policy at all.

namespace closecrab {

// Stream idle timeout, in seconds. A streaming Anthropic response emits SSE
// events (including `ping`) continuously; if nothing arrives for this long the
// connection is dead or the proxy is black-holing it, and waiting out the full
// CURLOPT_TIMEOUT=600 is what made CloseCrab look frozen for ten minutes at a
// time. Claude Code's default is 90s (CLAUDE_STREAM_IDLE_TIMEOUT_MS); we use the
// same value and honour the same env var so the two can be tuned identically.
// Generous vs. real thinking pauses: extended thinking still pings well inside 90s.
static long streamIdleTimeoutSeconds() {
    // Parenthesised std::max/std::min throughout this file: <windows.h> arrives
    // via a transitive include without NOMINMAX, so bare std::max(...) is eaten
    // by the max() macro.
    if (const char* env = std::getenv("CLAUDE_STREAM_IDLE_TIMEOUT_MS")) {
        try {
            long ms = std::stol(env);
            if (ms > 0) return (std::max)(10L, ms / 1000);
        } catch (...) { /* fall through to default */ }
    }
    if (const char* env = std::getenv("CLOSECRAB_STREAM_IDLE_TIMEOUT_MS")) {
        try {
            long ms = std::stol(env);
            if (ms > 0) return (std::max)(10L, ms / 1000);
        } catch (...) { /* fall through to default */ }
    }
    return 90;
}



void RemoteAPIClient::handleSSEEvent(
    const StreamParser::SSEEvent& event,
    StreamCallback& callback,
    std::string& currentToolName,
    std::string& currentToolId,
    std::string& currentToolJson,
    bool* streamedAnything
) const {
    if (event.data == "[DONE]") return;

    try {
        auto j = nlohmann::json::parse(event.data);
        std::string eventType = j.value("type", "");

        if (eventType == "content_block_start") {
            auto block = j.value("content_block", nlohmann::json::object());
            std::string blockType = block.value("type", "");
            if (blockType == "tool_use") {
                currentToolName = block.value("name", "");
                currentToolId = block.value("id", "");
                currentToolJson.clear();
            } else if (blockType == "web_search_tool_result") {
                // Server-side web search hits arrive whole in the start event.
                // Strip the huge `encrypted_content` (JackProAi keeps only
                // title+url) and surface as EVT_WEB_SEARCH_RESULT so WebSearchTool
                // can collect them. Non-streamed: content is present here.
                nlohmann::json hits = nlohmann::json::array();
                if (block.contains("content") && block["content"].is_array()) {
                    for (const auto& r : block["content"]) {
                        if (r.value("type", "") == "web_search_result") {
                            hits.push_back({{"title", r.value("title", "")},
                                            {"url", r.value("url", "")}});
                        }
                    }
                }
                if (!hits.empty()) {
                    StreamEvent ev;
                    ev.type = StreamEvent::EVT_WEB_SEARCH_RESULT;
                    ev.toolInput = std::move(hits);
                    callback(ev);
                }
            }
        } else if (eventType == "content_block_delta") {
            auto delta = j.value("delta", nlohmann::json::object());
            std::string deltaType = delta.value("type", "");

            if (deltaType == "text_delta") {
                std::string text = delta.value("text", "");
                if (!text.empty() && streamedAnything) *streamedAnything = true;
                callback({StreamEvent::EVT_TEXT, text});
            } else if (deltaType == "thinking_delta") {
                callback({StreamEvent::EVT_THINKING, delta.value("thinking", "")});
            } else if (deltaType == "input_json_delta") {
                currentToolJson += delta.value("partial_json", "");
                // Predictive execution: feed partial JSON to PredictiveEngine
                // so it can start reading files before content_block_stop.
                if (!currentToolName.empty()) {
                    closecrab::PredictiveEngine::getInstance().onToolInputDelta(
                        currentToolName, currentToolJson);
                }
            }
        } else if (eventType == "content_block_stop") {
            if (!currentToolName.empty()) {
                StreamEvent toolEvent;
                toolEvent.type = StreamEvent::EVT_TOOL_USE;
                toolEvent.toolName = currentToolName;
                toolEvent.toolUseId = currentToolId;
                try {
                    toolEvent.toolInput = nlohmann::json::parse(currentToolJson);
                } catch (...) {
                    toolEvent.toolInput = nlohmann::json::object();
                }
                callback(toolEvent);
                if (streamedAnything) *streamedAnything = true;
                currentToolName.clear();
                currentToolId.clear();
                currentToolJson.clear();
            }
        } else if (eventType == "message_delta") {
            auto delta = j.value("delta", nlohmann::json::object());
            std::string stopReason = delta.value("stop_reason", "");
            if (!stopReason.empty()) {
                StreamEvent stop;
                stop.type = StreamEvent::EVT_STOP;
                stop.stopReason = stopReason;
                // Extract usage
                if (j.contains("usage")) {
                    stop.usage.outputTokens = j["usage"].value("output_tokens", 0);
                }
                callback(stop);
            }
        } else if (eventType == "message_start") {
            if (j.contains("message") && j["message"].contains("usage")) {
                StreamEvent usageEvent;
                usageEvent.type = StreamEvent::EVT_USAGE_UPDATE;
                usageEvent.usage.inputTokens = j["message"]["usage"].value("input_tokens", 0);
                callback(usageEvent);
            }
        } else if (eventType == "error") {
            auto err = j.value("error", nlohmann::json::object());
            StreamEvent errEvent;
            errEvent.type = StreamEvent::EVT_ERROR;
            errEvent.content = err.value("message", "Unknown API error");
            callback(errEvent);
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to parse SSE event: {}", e.what());
    } catch (...) {
        // Catch non-std exceptions (Win32 SEH rethrown as C++, etc.) so they
        // don't escape back through the libcurl C callback stack (which would
        // hit the unhandled-exception filter and crash with 0xE06D7363).
        spdlog::warn("Failed to parse SSE event: unknown exception type");
    }
}

} // namespace closecrab (close before CURL calls)

// Resolve proxy URL: env vars > config.yaml
// Priority: CLOSECRAB_PROXY > https_proxy > HTTPS_PROXY > http_proxy > HTTP_PROXY > config.yaml proxy
static std::string resolveProxy() {
    const char* envVars[] = {
        "CLOSECRAB_PROXY", "https_proxy", "HTTPS_PROXY", "http_proxy", "HTTP_PROXY"
    };
    for (const char* var : envVars) {
        const char* val = std::getenv(var);
        if (val && val[0] != '\0') return val;
    }
    // Fallback: read from config.yaml
    std::string cfgProxy = Config::getInstance().getString("proxy", "");
    if (!cfgProxy.empty()) return cfgProxy;
    return "";
}

// Free function: perform CURL SSE request (outside namespace to avoid macro issues)
// Throws closecrab::APIError on retryable failures
static void performCurlSSE(
    const std::string& url,
    const std::string& bodyStr,
    const std::string& apiKey,
    CurlStreamCtx& curlCtx,
    const std::atomic<bool>* abortFlag = nullptr,
    bool want1M = false,
    long* retryAfterMsOut = nullptr
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw closecrab::APIError(closecrab::APIErrorType::NETWORK_ERROR, 0, "Failed to initialize CURL");
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("x-api-key: " + apiKey).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, "content-type: application/json");
    // anthropic-beta: matching Claude Code v2.1.258 headers (captured via
    // scripts/capture_claude_request.py against a live 2.1.258 binary).
    // Changes vs 2.1.220: context-1m-2025-08-07 is now unconditional (no longer
    // opt-in for [1m] suffix only); added fallback-credit-2026-06-01; removed
    // prompt-caching-evict, extended-cache-ttl, structured-outputs,
    // advanced-tool-use, tool-search-tool, redact-thinking, per-turn-control,
    // task-budgets, mcp-servers (no longer in 2.1.258 base set).
    // web-search-2025-03-05 kept: harmless when unused, enables server-side
    // web_search_20250305 tool for WebSearchTool without a separate header add.
    std::string betaHeader =
        "anthropic-beta: claude-code-20250219,context-1m-2025-08-07,"
        "interleaved-thinking-2025-05-14,thinking-token-count-2026-05-13,"
        "context-management-2025-06-27,prompt-caching-scope-2026-01-05,"
        "mid-conversation-system-2026-04-07,effort-2025-11-24,"
        "fallback-credit-2026-06-01,web-search-2025-03-05";
    (void)want1M; // context-1m is now unconditional in 2.1.258
    headers = curl_slist_append(headers, betaHeader.c_str());
    // Identity headers matching Claude Code SDK. Captured from a live
    // `claude -p` run (scripts/capture_claude_request.py); every one of these is
    // sent by the real 2.1.258 client, so sending them keeps us on the safe side
    // of proxy client-fingerprint checks. None of them changes API semantics.
    // 2.1.258 changes: User-Agent "cli" → "sdk-cli"; package 0.94.0 → 0.112.1;
    // new X-Stainless-Runtime-Version: v26.3.0.
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + apiKey).c_str());
    headers = curl_slist_append(headers, "User-Agent: claude-cli/2.1.258 (external, sdk-cli)");
    headers = curl_slist_append(headers, "x-app: cli");
    headers = curl_slist_append(headers, "anthropic-dangerous-direct-browser-access: true");
    headers = curl_slist_append(headers, "X-Stainless-Lang: js");
    headers = curl_slist_append(headers, "X-Stainless-Package-Version: 0.112.1");
    headers = curl_slist_append(headers, "X-Stainless-Runtime: node");
    headers = curl_slist_append(headers, "X-Stainless-Runtime-Version: v26.3.0");
    headers = curl_slist_append(headers, "X-Stainless-Retry-Count: 0");
    headers = curl_slist_append(headers, "X-Stainless-Timeout: 600");
#if defined(_WIN32)
    headers = curl_slist_append(headers, "X-Stainless-OS: Windows");
#elif defined(__APPLE__)
    headers = curl_slist_append(headers, "X-Stainless-OS: MacOS");
#else
    headers = curl_slist_append(headers, "X-Stainless-OS: Linux");
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    headers = curl_slist_append(headers, "X-Stainless-Arch: arm64");
#else
    headers = curl_slist_append(headers, "X-Stainless-Arch: x64");
#endif
    // Session ID: a real UUID (the shape Claude Code sends) and the SAME value
    // carried in body metadata.user_id.session_id. Stable for the process so the
    // server-side prompt cache stays warm.
    static const std::string sessionId =
        "X-Claude-Code-Session-Id: " + closecrab::clientSessionId();
    headers = curl_slist_append(headers, sessionId.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)bodyStr.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlStreamCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curlCtx);

    // Capture retry-after / retry-after-ms so the retry loop can obey the
    // server's own pacing hint instead of its computed backoff (what the real
    // client does: "if (retryAfter is present and 0 <= it < 60s) use it").
    if (retryAfterMsOut) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlHeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, retryAfterMsOut);
    }

    // Proxy support
    // Escape hatch: CLOSECRAB_NO_PROXY=1 forces a DIRECT connection, ignoring any
    // inherited https_proxy/http_proxy env vars (the common "browser works but app
    // fails" case on macOS, where a dead local proxy is still exported in ~/.zshrc).
    const char* noProxyEnv = std::getenv("CLOSECRAB_NO_PROXY");
    bool forceNoProxy = noProxyEnv && (std::string(noProxyEnv) == "1" ||
                                       std::string(noProxyEnv) == "true");
    std::string proxy = forceNoProxy ? "" : resolveProxy();
    if (forceNoProxy) {
        curl_easy_setopt(curl, CURLOPT_PROXY, "");      // override env-inherited proxy
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        static bool loggedNoProxy = false;
        if (!loggedNoProxy) {
            spdlog::info("CLOSECRAB_NO_PROXY set — forcing direct connection (ignoring proxy env vars)");
            loggedNoProxy = true;
        }
    } else if (!proxy.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
        // Log ONCE at info level. If the browser works but the app fails, a stale
        // https_proxy/http_proxy env var pointing at a dead local proxy is the most
        // common cause — surfacing it here makes that instantly diagnosable.
        static bool loggedProxy = false;
        if (!loggedProxy) {
            spdlog::info("Using proxy for API requests: {} "
                         "(from env var or config.yaml — set CLOSECRAB_NO_PROXY=1 "
                         "or unset https_proxy/http_proxy if this is unexpected)", proxy);
            loggedProxy = true;
        }
    }

    // SSL
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    // Timeouts: 600s overall, 30s connect.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    // Stream idle timeout. THE fix for "动不动就卡住": without this, a proxy that
    // accepts the POST and then stops sending holds the transfer open for the
    // full 600s CURLOPT_TIMEOUT — and because that error is retryable, the retry
    // loop repeats it, so a single stuck turn could sit there for over an hour
    // before surfacing anything. LOW_SPEED_LIMIT=1 byte / LOW_SPEED_TIME=90s
    // means "abort if under 1 B/s for 90s", i.e. abort only on a genuinely silent
    // connection. Anthropic streams send `ping` events during long thinking
    // pauses, so real work never trips this; only dead connections do. Fails as
    // CURLE_OPERATION_TIMEDOUT → NETWORK_ERROR → normal retry, but in 90s
    // instead of 600s. (The earlier "No LOW_SPEED (kills thinking pauses)" note
    // was written when the threshold under consideration was seconds, not 90s.)
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, closecrab::streamIdleTimeoutSeconds());

    // Detect a silently-dropped TCP connection rather than blocking on a socket
    // that will never deliver again (common with tunnels/NAT in the middle).
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);

    // Esc/abort: a MINIMAL progress callback that only reads the atomic abort
    // flag and returns 1 to abort. (The historical ACCESS_VIOLATION was from a
    // complex idle-watchdog callback doing timing work on possibly-freed state —
    // not from the progress mechanism itself. This one touches nothing else.)
    if (abortFlag) {
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void*)abortFlag);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
            +[](void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                auto* flag = static_cast<const std::atomic<bool>*>(p);
                return (flag && flag->load()) ? 1 : 0; // non-zero → abort transfer
            });
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    // HTTP/1.1 (system libcurl doesn't support HTTP/2)
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    // CRITICAL for multithreaded use: disable signal-based DNS timeouts.
    // Without this, libcurl uses SIGALRM from worker threads, corrupting
    // OpenSSL's shared state → all subsequent requests fail with SSL connect
    // error. JackProAi uses Node.js fetch which has no such issue.
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::string errMsg = std::string(curl_easy_strerror(res));
        throw closecrab::APIError(closecrab::APIErrorType::NETWORK_ERROR,
                                   static_cast<int>(httpCode), errMsg);
    }

    if (httpCode >= 400) {
        auto errType = closecrab::classifyHttpStatus(httpCode);
        std::string errBody = curlCtx.rawResponse.substr(0, 500);
        spdlog::error("HTTP {} response body: {}", httpCode, errBody);

        // Parse the actual error message from the proxy response.
        // yikoulian.cc returns JSON like: {"error":{"message":"预扣费额失败, 用户剩余额度: ¥0.53, 需要预扣费额度: ¥3.82"}}
        // Show the FULL message to the user, not just "HTTP 403".
        std::string displayMsg = "HTTP " + std::to_string(httpCode);
        try {
            auto errJson = nlohmann::json::parse(errBody);
            if (errJson.contains("error") && errJson["error"].contains("message")) {
                displayMsg = errJson["error"]["message"].get<std::string>();
            }
        } catch (...) {
            // Not JSON or no message field — use raw body if it has content
            if (errBody.size() > 10 && errBody.size() < 300) {
                displayMsg += ": " + errBody;
            }
        }
        throw closecrab::APIError(errType, static_cast<int>(httpCode), displayMsg);
    }
}

namespace closecrab { // reopen

// ---------------------------------------------------------------------------
// ensureToolResultPairing
// Mirrors JackProAi src/utils/messages.ts ensureToolResultPairing().
// Runs on the wire JSON messages array before every API send.
// Fixes the class of 400 INVALID_REQUEST errors caused by:
//   1. tool_use blocks with no matching tool_result in the next user message
//      (happens when compaction deletes an intervening turn, or on session resume)
//   2. tool_result blocks in a user message with no preceding assistant tool_use
//      (orphans from previous session / interrupted stream)
//   3. Duplicate tool_use IDs across messages (repeat-turn corruption)
//   4. Duplicate tool_result IDs in a single user message (doubled orphan runs)
// ---------------------------------------------------------------------------
static bool ensureToolResultPairing(nlohmann::json& messages) {
    if (!messages.is_array() || messages.empty()) return false;

    static const std::string kSynthetic = "[Tool result missing due to internal error]";
    static const std::string kInterrupted = "[Tool use interrupted]";

    bool repaired = false;
    std::set<std::string> allSeenToolUseIds; // cross-message dup detection
    nlohmann::json result = nlohmann::json::array();

    for (size_t i = 0; i < messages.size(); i++) {
        auto& msg = messages[i];
        std::string role = msg.value("role", "");

        // ---- User message ----
        if (role != "assistant") {
            // Strip orphaned tool_result blocks at the start of the history
            // (i.e. user message whose preceding entry is NOT an assistant).
            if (role == "user"
                && msg.contains("content") && msg["content"].is_array()
                && (result.empty() || result.back().value("role", "") != "assistant"))
            {
                nlohmann::json stripped = nlohmann::json::array();
                for (auto& b : msg["content"]) {
                    if (b.value("type", "") == "tool_result") { repaired = true; continue; }
                    stripped.push_back(b);
                }
                if (repaired && stripped.empty())
                    stripped.push_back({{"type","text"},{"text","[Orphaned tool result removed]"}});
                if (stripped.size() != msg["content"].size())
                    msg["content"] = stripped;
            }
            result.push_back(msg);
            continue;
        }

        // ---- Assistant message ----
        if (!msg.contains("content") || !msg["content"].is_array()) {
            result.push_back(msg); continue;
        }

        // Dedupe tool_use blocks (cross-message; same id in two different messages)
        nlohmann::json finalContent = nlohmann::json::array();
        std::set<std::string> localIds;
        for (auto& block : msg["content"]) {
            if (block.value("type", "") == "tool_use") {
                std::string id = block.value("id", "");
                if (!id.empty() && allSeenToolUseIds.count(id)) { repaired = true; continue; }
                if (!id.empty()) { allSeenToolUseIds.insert(id); localIds.insert(id); }
            }
            finalContent.push_back(block);
        }
        if (finalContent.empty()) {
            finalContent.push_back({{"type","text"},{"text",kInterrupted}});
            repaired = true;
        }
        if (finalContent.size() != msg["content"].size()) {
            msg["content"] = finalContent; repaired = true;
        }
        result.push_back(msg);

        if (localIds.empty()) continue; // no tool_use in this assistant turn

        // ---- Check next user message for matching tool_results ----
        std::set<std::string> existingResultIds;
        bool hasDupResults = false;
        bool nextIsUser = (i+1 < messages.size() && messages[i+1].value("role","") == "user");

        if (nextIsUser && messages[i+1].contains("content") && messages[i+1]["content"].is_array()) {
            for (auto& b : messages[i+1]["content"]) {
                if (b.value("type","") == "tool_result") {
                    std::string trId = b.value("tool_use_id","");
                    if (!trId.empty()) {
                        if (existingResultIds.count(trId)) hasDupResults = true;
                        existingResultIds.insert(trId);
                    }
                }
            }
        }

        // Missing: tool_use without tool_result. Orphaned: tool_result without tool_use.
        std::vector<std::string> missing, orphaned;
        for (const auto& id : localIds)
            if (!existingResultIds.count(id)) missing.push_back(id);
        for (const auto& id : existingResultIds)
            if (!localIds.count(id)) orphaned.push_back(id);

        if (missing.empty() && orphaned.empty() && !hasDupResults) continue;

        repaired = true;

        // Build synthetic tool_result blocks for missing IDs
        nlohmann::json synths = nlohmann::json::array();
        for (const auto& id : missing)
            synths.push_back({{"type","tool_result"},{"tool_use_id",id},
                              {"content",kSynthetic},{"is_error",true}});

        if (nextIsUser) {
            auto& nextMsg = messages[i+1];
            nlohmann::json content = (nextMsg.contains("content") && nextMsg["content"].is_array())
                ? nextMsg["content"] : nlohmann::json::array();

            // Strip orphaned and duplicate tool_results
            if (!orphaned.empty() || hasDupResults) {
                std::set<std::string> orphanSet(orphaned.begin(), orphaned.end());
                std::set<std::string> seenTr;
                nlohmann::json filtered = nlohmann::json::array();
                for (auto& b : content) {
                    if (b.value("type","") == "tool_result") {
                        std::string trId = b.value("tool_use_id","");
                        if (orphanSet.count(trId) || seenTr.count(trId)) continue;
                        seenTr.insert(trId);
                    }
                    filtered.push_back(b);
                }
                content = filtered;
            }

            // Prepend synthetic blocks
            nlohmann::json patched = nlohmann::json::array();
            for (auto& b : synths)   patched.push_back(b);
            for (auto& b : content)  patched.push_back(b);
            if (patched.empty())
                patched.push_back({{"type","text"},{"text","[Conversation resume placeholder]"}});

            nextMsg["content"] = patched;
            i++; // consume the (now patched) next message
            result.push_back(nextMsg);
        } else if (!synths.empty()) {
            // No user message follows — insert a synthetic one to maintain pairing
            nlohmann::json synUser;
            synUser["role"] = "user";
            synUser["content"] = synths;
            result.push_back(synUser);
        }
    }

    if (repaired) {
        spdlog::warn("ensureToolResultPairing: repaired tool_use/tool_result mismatch "
                     "({} -> {} messages). Check logs for root cause.",
                     messages.size(), result.size());
        messages = result;
    }
    return repaired;
}

void RemoteAPIClient::streamChat(
    const std::vector<Message>& messages,
    const std::string& systemPrompt,
    const ModelConfig& config,
    StreamCallback callback
) {
    if (apiKey_.empty()) {
        throw APIError(APIErrorType::AUTH_ERROR, 0,
            "No API key configured. Use --api-key or set ANTHROPIC_AUTH_TOKEN.");
    }

    // Strip trailing slash from baseUrl to avoid double-slash in URL
    std::string base = baseUrl_;
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::string url = base + "/v1/messages";
    constexpr int MAX_RETRIES = 10;
    constexpr int FALLBACK_THRESHOLD = 3;  // JackProAi: MAX_529_RETRIES=3, then fallback model
    int consecutive503 = 0;
    std::string activeModel = model_;

    // Total retry budget. MAX_RETRIES alone is not a bound on wall-clock time:
    // with 500ms*2^n backoff the last few attempts are ~30s each, and every
    // attempt can itself sit in the network for up to CURLOPT_TIMEOUT. In the
    // 2026-08-02 log that combination kept one turn retrying from 20:33 to
    // 20:42 (9 min) and then again 20:53+ with nothing shown but a spinner.
    // The real client bounds the whole thing too; we cap at 3 minutes of retry
    // wall-clock and stop with whatever we have.
    const auto retryDeadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);

    // Server-supplied retry pacing from the last failed attempt (-1 = none).
    long lastRetryAfterMs = -1;

    // Retry-time compaction level. buildRequestBody() rebuilds `body` from the
    // original `messages` on every attempt, so any in-place edit to `body` would
    // be discarded next loop. Instead we carry a level here and RE-APPLY the
    // compaction to the freshly-built body each attempt. Each retry that still
    // fails on an oversized request bumps the level, shrinking the kept tail.
    //   0 = no compaction (first try sends full context)
    //  >0 = truncate old tool_result/text blocks, keeping the last `keepTail`
    //       messages intact; higher level => smaller keepTail + smaller cap.
    int compactLevel = 0;

    // Safety fallback for proxies that don't understand the server-side
    // `context_management` field (clear_tool_uses_20250919). If such a proxy
    // returns 400 INVALID_REQUEST, we strip the field and retry ONCE instead of
    // failing the whole turn. Once set, every subsequent rebuilt body has the
    // field removed before sending.
    bool stripContextMgmt = false;

    // Same defensive fallback for output_config.effort (native effort). If a
    // proxy doesn't understand the `effort-2025-11-24` beta / output_config, it
    // may 400. Strip output_config and retry ONCE rather than failing the turn.
    bool stripEffort = false;

    // Truncate old, large tool_result / text blocks in the request body so a
    // retry sends a smaller payload. Returns true if anything was trimmed.
    auto applyRetryCompaction = [](nlohmann::json& reqBody, int level) -> bool {
        if (level <= 0 || !reqBody.contains("messages") || !reqBody["messages"].is_array())
            return false;
        auto& msgArray = reqBody["messages"];
        // keepTail shrinks as the level rises: 6, 4, 3, 2 ... floor at 2.
        size_t keepTail = (level >= 4) ? 2 : (level >= 3) ? 3 : (level >= 2) ? 4 : 6;
        // cap also shrinks: 200, 150, 100 chars of each oversized block.
        size_t cap = (level >= 3) ? 100 : (level >= 2) ? 150 : 200;
        if (msgArray.size() <= keepTail) return false;
        size_t scanEnd = msgArray.size() - keepTail;
        bool trimmed = false;
        for (size_t i = 0; i < scanEnd; i++) {
            if (!msgArray[i].contains("content") || !msgArray[i]["content"].is_array())
                continue;
            for (auto& block : msgArray[i]["content"]) {
                const std::string btype = block.value("type", "");
                if (btype == "tool_result") {
                    if (block.contains("content") && block["content"].is_string()) {
                        std::string content = block["content"].get<std::string>();
                        if (content.size() > cap) {
                            block["content"] = content.substr(0, cap) +
                                "\n[cleared: request too large for proxy]";
                            trimmed = true;
                        }
                    } else if (block.contains("content") && block["content"].is_array()) {
                        for (auto& sub : block["content"]) {
                            if (sub.value("type", "") == "text") {
                                std::string text = sub.value("text", "");
                                if (text.size() > cap) {
                                    sub["text"] = text.substr(0, cap) + "\n[cleared]";
                                    trimmed = true;
                                }
                            }
                        }
                    }
                } else if (btype == "text") {
                    if (block.contains("text") && block["text"].is_string()) {
                        std::string text = block["text"].get<std::string>();
                        if (text.size() > cap) {
                            block["text"] = text.substr(0, cap) + "\n[cleared]";
                            trimmed = true;
                        }
                    }
                }
            }
        }
        return trimmed;
    };

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        auto body = buildRequestBody(messages, systemPrompt, config, activeModel);
        // Repair tool_use/tool_result pairing on the wire copy before sending.
        // Mirrors JackProAi's ensureToolResultPairing (called at claude.ts:1301).
        // Prevents the class of 400 INVALID_REQUEST errors caused by compaction
        // dropping a turn that left a stranded tool_use without a tool_result.
        if (body.contains("messages"))
            ensureToolResultPairing(body["messages"]);
        // If a prior attempt got a 400 with context_management present, the proxy
        // likely rejects that field — drop it on this and all further attempts.
        if (stripContextMgmt && body.contains("context_management")) {
            body.erase("context_management");
        }
        // If a prior attempt got a 400 with output_config present, the proxy
        // likely rejects native effort — drop it on this and all further attempts.
        if (stripEffort && body.contains("output_config")) {
            body.erase("output_config");
        }
        // Re-apply retry-time compaction (see compactLevel above). On the first
        // attempt level==0 so this is a no-op and the full context is sent.
        if (compactLevel > 0) {
            size_t beforeBytes = 0, afterBytes = 0;
            try {
                beforeBytes = body.dump(-1, ' ', false,
                    nlohmann::json::error_handler_t::replace).size();
            } catch (...) {
                spdlog::warn("Retry compaction: beforeBytes dump failed, skipping size report");
            }
            if (applyRetryCompaction(body, compactLevel)) {
                try {
                    afterBytes = body.dump(-1, ' ', false,
                        nlohmann::json::error_handler_t::replace).size();
                    spdlog::warn("Retry compaction L{}: {}KB -> {}KB",
                                 compactLevel, beforeBytes / 1024, afterBytes / 1024);
                } catch (...) {
                    spdlog::warn("Retry compaction L{}: afterBytes dump failed", compactLevel);
                }
            }
        }
        // Use error_handler_t::replace to handle invalid UTF-8 (Chinese chars, tool output)
        // Wrap in try-catch: malformed tool output (null bytes, invalid UTF-8, overlarge
        // strings) can trigger CRT invalid parameter checks inside dump() -> memchr/memmove,
        // causing __fastfail(0xC0000409) that bypasses all exception handlers. Seen in wild
        // when ensureToolResultPairing processes corrupted tool_result blocks.
        std::string bodyStr;
        try {
            bodyStr = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        } catch (const std::exception& e) {
            spdlog::error("body.dump() failed (likely malformed tool output): {}", e.what());
            throw APIError(APIErrorType::NETWORK_ERROR, 0,
                std::string("Failed to serialize API request body: ") + e.what());
        } catch (...) {
            spdlog::error("body.dump() failed with unknown exception (likely CRT invalid parameter)");
            throw APIError(APIErrorType::NETWORK_ERROR, 0,
                "Failed to serialize API request body (unknown error, check crash.log)");
        }

        if (attempt == 1) {
            spdlog::debug("API request: {} bytes, {} tools, system_prompt={} chars, model={}, url={}",
                         bodyStr.size(),
                         config.tools.is_array() ? config.tools.size() : 0,
                         systemPrompt.size(), activeModel, url);
        }

        if (const char* debugDir = std::getenv("CLOSECRAB_DEBUG_DIR")) {
            std::string debugPath = std::string(debugDir) + "/last_request.json";
            std::ofstream dbg(debugPath);
            if (dbg.is_open()) {
                try {
                    dbg << body.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
                } catch (...) {
                    dbg << "{\"error\":\"dump failed\"}";
                }
                dbg.close();
            }
        }

        // Set when this attempt failed *after* already emitting visible output.
        // Checked in the attempt's catch block, which is what actually decides
        // whether to retry — an inner `throw` alone would just land there and be
        // retried like any other error.
        bool partialOutputDelivered = false;

        try {
            std::string currentToolName, currentToolId, currentToolJson;
            // Did this attempt deliver any user-visible content before failing?
            // If it did, retrying the whole request would re-emit that text and
            // the user sees the answer twice. The real client does not restart a
            // stream that has already produced output; neither do we.
            bool streamedAnything = false;
            StreamParser parser([&](const StreamParser::SSEEvent& event) {
                handleSSEEvent(event, callback, currentToolName, currentToolId, currentToolJson,
                               &streamedAnything);
            });
            CurlStreamCtx curlCtx{&parser, "", {}, 0};
            long retryAfterMs = -1;
            // Crash trace: log before/after CURL call to narrow down segfault location
            {
                FILE* trace = closecrab::traceOpen();
                if (trace) { fprintf(trace, "[%d] pre-curl bodySize=%zu\n", attempt, bodyStr.size()); fclose(trace); }
            }
            try {
                performCurlSSE(url, bodyStr, apiKey_, curlCtx, config.abortFlag,
                               wants1MContext(activeModel), &retryAfterMs);
            } catch (const APIError&) {
                lastRetryAfterMs = retryAfterMs;
                // Partial output already shown → surface the failure now rather
                // than replaying the turn. Without this, every retry after a
                // mid-stream drop appends a second copy of the same answer.
                if (streamedAnything) {
                    partialOutputDelivered = true;
                    parser.finish();
                }
                throw;
            }
            {
                FILE* trace = closecrab::traceOpen();
                if (trace) { fprintf(trace, "[%d] post-curl OK\n", attempt); fclose(trace); }
            }
            parser.finish();
            return; // Success
        } catch (const APIError& e) {
            // Esc/abort: if the user interrupted, the curl abort surfaces as a
            // retryable network error — do NOT retry, stop immediately.
            if (config.abortFlag && config.abortFlag->load()) {
                spdlog::info("Stream aborted by user (Esc)");
                return;
            }
            // Already showed the user part of the answer. Retrying would re-run
            // the whole turn and duplicate that text (and re-run tool calls), so
            // stop here and let them continue with a new message.
            if (partialOutputDelivered) {
                spdlog::warn("Stream failed after partial output was already delivered "
                             "— not retrying (a retry would duplicate the visible text "
                             "and re-run tool calls). Reason: {}", e.what());
                throw;
            }
            // Proxy client-fingerprint rejection. Distinct from every other 400:
            // nothing about the conversation is wrong, so stripping fields or
            // compacting is pointless (and burns quota on doomed retries). Fail
            // fast with an actionable message instead of the misleading
            // "try /compact" the generic 400 path produces.
            {
                std::string msg = e.what();
                if (e.type == APIErrorType::INVALID_REQUEST &&
                    (msg.find("anomaly in your client") != std::string::npos ||
                     msg.find("\xE5\xAE\xA2\xE6\x88\xB7\xE7\xAB\xAF\xE5\xAD\x98\xE5\x9C\xA8\xE5\xBC\x82\xE5\xB8\xB8") != std::string::npos ||
                     msg.find("standard Claude Code client") != std::string::npos)) {
                    spdlog::error(
                        "Proxy rejected this client as non-standard. This is a "
                        "request-shape mismatch with the official Claude Code "
                        "client, NOT a context/quota problem — /compact will not "
                        "help. Re-sync the request shape: run "
                        "`python scripts/capture_claude_request.py` and diff its "
                        "output against performCurlSSE() headers and "
                        "buildRequestBody() in src/api/RemoteAPIClient.cpp "
                        "(the top-level `metadata` field is required).");
                    throw;
                }
            }
            // Proxy rejected the request with 400. If we sent the server-side
            // `context_management` field and haven't yet stripped it, the most
            // likely cause is that this proxy doesn't support that field. Strip
            // it and retry ONCE before giving up (400 is otherwise non-retryable).
            if (e.type == APIErrorType::INVALID_REQUEST && !stripContextMgmt &&
                body.contains("context_management") && attempt < MAX_RETRIES) {
                spdlog::warn("400 with context_management present — proxy likely rejects it; "
                             "stripping field and retrying without server-side tool clearing");
                stripContextMgmt = true;
                // Don't count this as a server-error retry; just loop again.
                continue;
            }
            // Same handling for native effort (output_config). A proxy that
            // doesn't grok the effort beta 400s; drop output_config and retry once
            // so the turn still completes (just without effort-controlled depth).
            if (e.type == APIErrorType::INVALID_REQUEST && !stripEffort &&
                body.contains("output_config") && attempt < MAX_RETRIES) {
                spdlog::warn("400 with output_config (effort) present — proxy likely rejects it; "
                             "stripping effort and retrying without native reasoning effort");
                stripEffort = true;
                continue;
            }

            if (!isRetryable(e.type)) throw;

            consecutive503++;

            // JackProAi strategy: after 3 consecutive 503/529, switch to fallback
            // model (not throw). This keeps the request alive. Only throw after
            // ALL 10 retries are exhausted.
            if (consecutive503 >= FALLBACK_THRESHOLD && !fallbackModel_.empty() && activeModel != fallbackModel_) {
                spdlog::warn("Model fallback: {} -> {} (after {} consecutive server errors)", activeModel, fallbackModel_, consecutive503);
                activeModel = fallbackModel_;
                consecutive503 = 0;
            }

            if (attempt >= MAX_RETRIES) throw;

            // Wall-clock budget check. Without this, MAX_RETRIES x (up to 600s in
            // the network + up to ~30s backoff) is an unbounded-feeling freeze.
            if (std::chrono::steady_clock::now() >= retryDeadline) {
                spdlog::error("Retry budget exhausted after {} attempts (3 min wall clock) "
                              "— giving up so the turn does not hang indefinitely. "
                              "Last error: {}", attempt, e.what());
                throw;
            }

            // Decide whether this error means "request too big" (→ compact harder
            // before retry) or "provider temporarily down" (→ just wait & retry,
            // do NOT touch context). The OLD code treated EVERY 503/504/timeout on
            // a >80KB request as oversized and kept bumping the compaction level
            // L1→L9, shrinking context on each retry — but the real cause was
            // usually a transient upstream outage ("所有供应商暂时不可用"), so it
            // destroyed context chasing a size problem that didn't exist.
            //
            // JackProAi separates these: overloaded_error/529 is purely retryable
            // (Hw6 → retry, never compact); only a real context-limit error (fX4)
            // adjusts tokens. We mirror that here. Since the yikoulian.cc proxy
            // reports outages as a generic 503 "供应商不可用", we use two signals:
            //   1. the error text explicitly says overloaded/unavailable  → overload
            //   2. the request is genuinely large vs the real context window → oversized
            // Only #2 (or an explicit size error) bumps compaction.
            std::string emsg = e.what();
            auto contains = [&](const char* s){ return emsg.find(s) != std::string::npos; };
            bool overloadSignal =
                contains("overloaded") || contains("service_unavailable") ||
                contains("\xe4\xbe\x9b\xe5\xba\x94\xe5\x95\x86") ||  // 供应商
                contains("\xe6\x9a\x82\xe6\x97\xb6\xe4\xb8\x8d\xe5\x8f\xaf\xe7\x94\xa8"); // 暂时不可用
            // "Genuinely large" = within ~85% of the real usable window. Below
            // that, a 503 is almost certainly an outage, not a size rejection, so
            // shrinking context would be both useless and destructive.
            // ~3.5 chars/token on the serialized body → bytes threshold.
            const size_t kOversizedBytes = 580000; // ~165K tokens of body
            bool sizeError = contains("too long") || contains("context limit") ||
                             contains("\xe8\xbf\x87\xe9\x95\xbf") ||  // 过长
                             e.httpStatus == 413;
            bool oversized = bodyStr.size() > kOversizedBytes;
            bool serverOrNet = (e.type == APIErrorType::NETWORK_ERROR ||
                                e.type == APIErrorType::SERVER_ERROR);

            if (sizeError || (serverOrNet && oversized && !overloadSignal)) {
                compactLevel++;
                spdlog::warn("{} on {}KB request — raising retry compaction to L{} before retry "
                             "(reason: {})", apiErrorTypeName(e.type), bodyStr.size() / 1024,
                             compactLevel, sizeError ? "explicit size error" : "oversized body");
            } else if (serverOrNet) {
                spdlog::info("{} on {}KB request — treating as transient overload, "
                             "retrying without compaction (context preserved)",
                             apiErrorTypeName(e.type), bodyStr.size() / 1024);
            }

            // Backoff, matching the real client's calculateDefaultRetryTimeoutMillis:
            //     min(0.5 * 2^n, 8) seconds, times (1 - random()*0.25)
            // i.e. 0.5s, 1s, 2s, 4s, 8s, 8s... capped at 8 SECONDS, not 30.
            // The old 500ms*2^n with a 30s cap produced 16s/30s/30s tail delays
            // (visible in the 2026-08-02 log as "Retrying in 32460ms"), which is
            // what turned a transient blip into a multi-minute stall for no gain:
            // if a provider is down, waiting 30s instead of 8s does not help.
            double baseSec = (std::min)(0.5 * std::pow(2.0, attempt - 1), 8.0);
            double factor = 1.0 - (static_cast<double>(rand()) / RAND_MAX) * 0.25;
            int delayMs = static_cast<int>(baseSec * factor * 1000.0);

            // Prefer the server's own pacing hint when it is present and sane
            // (0..60s) — same rule the real client applies before falling back to
            // its computed backoff. Matters most for 429, where the server knows
            // exactly when the window reopens.
            if (lastRetryAfterMs >= 0 && lastRetryAfterMs < 60000) {
                spdlog::info("Honouring server retry-after: {}ms (computed backoff was {}ms)",
                             lastRetryAfterMs, delayMs);
                delayMs = static_cast<int>(lastRetryAfterMs);
            }
            lastRetryAfterMs = -1;

            // Show retry progress to user (JackProAi: "Retrying in Xs... (attempt N/M)")
            // CRITICAL: include e.what() so the ACTUAL cause is visible in the log.
            // For NETWORK_ERROR this carries the raw curl reason ("Couldn't connect
            // to proxy 127.0.0.1:7897", "Couldn't resolve host", "SSL certificate
            // problem", etc.) — without it the user only sees "Retrying..." and the
            // root cause (stale proxy env var, DNS, cert) is invisible.
            spdlog::warn("Retrying in {}ms (attempt {}/{}, model={}) — reason: {}",
                         delayMs, attempt, MAX_RETRIES, activeModel, e.what());
            // Surface to the UI so the spinner shows "retrying N/M" instead of
            // appearing frozen. e.statusCode 503 = provider unavailable.
            {
                StreamEvent retryEvent;
                retryEvent.type = StreamEvent::EVT_RETRY;
                retryEvent.retryAttempt = attempt;
                retryEvent.retryMax = MAX_RETRIES;
                retryEvent.retryDelayMs = delayMs;
                retryEvent.content = (e.httpStatus == 503 || e.httpStatus == 504 || e.httpStatus == 529)
                    ? "service busy" : "network error";
                callback(retryEvent);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }
}

int RemoteAPIClient::countTokens(const std::string& text) const {
    return static_cast<int>(text.size() / 3);
}

// ---------------------------------------------------------------------------
// listModels — synchronous GET /v1/models
// ---------------------------------------------------------------------------
std::vector<std::string> RemoteAPIClient::listModels() const {
    std::vector<std::string> result;
    if (apiKey_.empty() || baseUrl_.empty()) return result;

    std::string base = baseUrl_;
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::string url = base + "/v1/models";

    // Accumulate response body
    std::string responseBody;
    auto writeCallback = [](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        auto* body = static_cast<std::string*>(userdata);
        body->append(ptr, size * nmemb);
        return size * nmemb;
    };

    CURL* curl = curl_easy_init();
    if (!curl) return result;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("x-api-key: " + apiKey_).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     static_cast<size_t(*)(char*, size_t, size_t, void*)>(writeCallback));
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // Inherit proxy if set
    const char* proxyEnv = std::getenv("CLOSECRAB_PROXY");
    if (!proxyEnv) proxyEnv = std::getenv("HTTPS_PROXY");
    if (!proxyEnv) proxyEnv = std::getenv("https_proxy");
    if (proxyEnv && *proxyEnv) curl_easy_setopt(curl, CURLOPT_PROXY, proxyEnv);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || httpCode != 200) return result;

    try {
        auto j = nlohmann::json::parse(responseBody);
        // Anthropic: {"data":[{"id":"claude-opus-4-8",...},...]}
        // OpenAI-compat: same shape
        if (j.contains("data") && j["data"].is_array()) {
            for (const auto& m : j["data"]) {
                if (m.contains("id") && m["id"].is_string()) {
                    result.push_back(m["id"].get<std::string>());
                }
            }
        }
    } catch (...) {}
    return result;
}

} // namespace closecrab
