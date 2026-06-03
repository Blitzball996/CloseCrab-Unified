#include "RemoteAPIClient.h"
#include "APIError.h"
#include "../config/Config.h"
#include "../core/PredictiveEngine.h"
#include "../utils/Trace.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <vector>

// JackProAi has NO explicit rate limiting — relies on server-side limits + fast retry.
// Removed global mutex serialization that was blocking concurrent agent requests.

namespace closecrab {

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
    body["model"] = modelOverride.empty() ? model_ : modelOverride;
    body["max_tokens"] = config.maxTokens;
    body["stream"] = config.stream;

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

    // Thinking
    if (config.thinkingEnabled) {
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
        const char* clearEnv = std::getenv("CLOSECRAB_API_CLEAR_TOOL_RESULTS");
        bool enableServerClear = clearEnv && (std::string(clearEnv) == "1" ||
                                              std::string(clearEnv) == "true");
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
            body["context_management"] = {{"edits", std::move(edits)}};
        }
    }

    return body;
}

} // namespace closecrab (temporarily close for CURL C callbacks)

struct CurlStreamCtx {
    closecrab::StreamParser* parser;
    std::string rawResponse;
};

static size_t curlStreamCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<CurlStreamCtx*>(userdata);
    size_t totalSize = size * nmemb;
    // CRITICAL: This is a C callback invoked by libcurl. If ANY exception
    // escapes here, it's undefined behavior (MSVC calls std::terminate →
    // instant crash with no error message). This was causing the "闪退 while
    // writing" bug — large streaming responses could trigger exceptions in
    // the parser or callback chain.
    try {
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
// Rely on CURLOPT_TIMEOUT=600 as the safety net instead.

namespace closecrab { // reopen

void RemoteAPIClient::handleSSEEvent(
    const StreamParser::SSEEvent& event,
    StreamCallback& callback,
    std::string& currentToolName,
    std::string& currentToolId,
    std::string& currentToolJson
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
                callback({StreamEvent::EVT_TEXT, delta.value("text", "")});
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
    const std::atomic<bool>* abortFlag = nullptr
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw closecrab::APIError(closecrab::APIErrorType::NETWORK_ERROR, 0, "Failed to initialize CURL");
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("x-api-key: " + apiKey).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, "content-type: application/json");
    // anthropic-beta: matching Claude Code v2.1.152 headers. web-search-2025-03-05
    // is harmless when no web_search tool is present (server ignores unused betas)
    // and enables the server-side web_search_20250305 tool when WebSearchTool uses it.
    headers = curl_slist_append(headers, "anthropic-beta: claude-code-20250219,interleaved-thinking-2025-05-14,prompt-caching-scope-2026-01-05,extended-cache-ttl-2025-04-11,context-management-2025-06-27,structured-outputs-2025-12-15,advanced-tool-use-2025-11-20,tool-search-tool-2025-10-19,redact-thinking-2026-02-12,mid-conversation-system-2026-04-07,mcp-servers-2025-12-04,web-search-2025-03-05");
    // Identity headers matching Claude Code SDK
    headers = curl_slist_append(headers, "User-Agent: claude-cli/2.1.152 (external, cli)");
    headers = curl_slist_append(headers, "x-app: cli");
    // Stable session ID for prompt cache affinity (NOT random - cache needs same session)
    static std::string sessionId = "X-Claude-Code-Session-Id: cc-" + std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count() / 1000000000);
    headers = curl_slist_append(headers, sessionId.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)bodyStr.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlStreamCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curlCtx);

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

    // Timeouts: 600s overall, 30s connect. No LOW_SPEED (kills thinking pauses).
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

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

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        auto body = buildRequestBody(messages, systemPrompt, config, activeModel);
        // Use error_handler_t::replace to handle invalid UTF-8 (Chinese chars, tool output)
        std::string bodyStr = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

        if (attempt == 1) {
            spdlog::debug("API request: {} bytes, {} tools, system_prompt={} chars, model={}, url={}",
                         bodyStr.size(),
                         config.tools.is_array() ? config.tools.size() : 0,
                         systemPrompt.size(), activeModel, url);
        }

        if (const char* debugDir = std::getenv("CLOSECRAB_DEBUG_DIR")) {
            std::string debugPath = std::string(debugDir) + "/last_request.json";
            std::ofstream dbg(debugPath);
            if (dbg.is_open()) { dbg << body.dump(2, ' ', false, nlohmann::json::error_handler_t::replace); dbg.close(); }
        }

        try {
            std::string currentToolName, currentToolId, currentToolJson;
            StreamParser parser([&](const StreamParser::SSEEvent& event) {
                handleSSEEvent(event, callback, currentToolName, currentToolId, currentToolJson);
            });
            CurlStreamCtx curlCtx{&parser, ""};
            // Crash trace: log before/after CURL call to narrow down segfault location
            {
                FILE* trace = closecrab::traceOpen();
                if (trace) { fprintf(trace, "[%d] pre-curl bodySize=%zu\n", attempt, bodyStr.size()); fclose(trace); }
            }
            performCurlSSE(url, bodyStr, apiKey_, curlCtx, config.abortFlag);
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

            // claude-code strategy: on network timeout, the request is likely too
            // large for the proxy. Compact messages BEFORE retrying so the next
            // attempt sends a smaller request. Without this, all 10 retries send
            // the same oversized request and all time out identically.
            if (e.type == APIErrorType::NETWORK_ERROR && bodyStr.size() > 80000) {
                spdlog::warn("Request was {}KB — compacting before retry", bodyStr.size() / 1024);
                auto& msgArray = body["messages"];
                for (size_t i = 0; i + 4 < msgArray.size(); i++) {
                    if (msgArray[i].value("role", "") == "user" && msgArray[i].contains("content") && msgArray[i]["content"].is_array()) {
                        for (auto& block : msgArray[i]["content"]) {
                            if (block.value("type", "") == "tool_result") {
                                if (block.contains("content") && block["content"].is_string()) {
                                    std::string content = block["content"].get<std::string>();
                                    if (content.size() > 200) {
                                        block["content"] = content.substr(0, 150) + "\n[cleared: request too large for proxy]";
                                    }
                                } else if (block.contains("content") && block["content"].is_array()) {
                                    for (auto& sub : block["content"]) {
                                        if (sub.value("type", "") == "text") {
                                            std::string text = sub.value("text", "");
                                            if (text.size() > 200) {
                                                sub["text"] = text.substr(0, 150) + "\n[cleared]";
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                bodyStr = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                spdlog::info("Compacted to {}KB", bodyStr.size() / 1024);
            }

            // Exponential backoff: 500ms base, cap 30s
            int baseDelay = 500 * (1 << (attempt - 1));
            if (baseDelay > 30000) baseDelay = 30000;
            int jitter = rand() % (baseDelay / 4 + 1);
            int delayMs = baseDelay + jitter;

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
                retryEvent.content = (e.httpStatus == 503 || e.httpStatus == 529)
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

} // namespace closecrab
