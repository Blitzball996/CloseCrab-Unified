#include "RemoteAPIClient.h"
#include "APIError.h"
#include "../config/Config.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <thread>

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

    // System prompt with cache_control
    if (!systemPrompt.empty()) {
        body["system"] = nlohmann::json::array({
            {{"type", "text"}, {"text", systemPrompt}, {"cache_control", {{"type", "ephemeral"}}}}
        });
    }

    // Messages
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& msg : messages) {
        msgs.push_back(msg.toApiJson());
    }

    // API Microcompact: clear old tool_result content when context is too large.
    // Threshold must match what the proxy can handle within its timeout.
    // See claude-code analysis below for the correct strategy.
    constexpr size_t MAX_MESSAGES_SIZE = 200000;
    std::string msgsStr = msgs.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (msgsStr.size() > MAX_MESSAGES_SIZE && msgs.size() > 4) {
        // Clear oldest tool_results first (keep last 4 messages intact)
        for (size_t i = 0; i + 4 < msgs.size(); i++) {
            if (msgs[i].value("role", "") == "user" && msgs[i].contains("content") && msgs[i]["content"].is_array()) {
                for (auto& block : msgs[i]["content"]) {
                    if (block.value("type", "") == "tool_result") {
                        if (block.contains("content") && block["content"].is_string()) {
                            std::string content = block["content"].get<std::string>();
                            if (content.size() > 500) {
                                block["content"] = content.substr(0, 400) + "\n[cleared for context limit]";
                            }
                        } else if (block.contains("content") && block["content"].is_array()) {
                            for (auto& sub : block["content"]) {
                                if (sub.value("type", "") == "text") {
                                    std::string text = sub.value("text", "");
                                    if (text.size() > 500) {
                                        sub["text"] = text.substr(0, 400) + "\n[cleared for context limit]";
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // Re-check after each message cleared
            msgsStr = msgs.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            if (msgsStr.size() <= MAX_MESSAGES_SIZE) break;
        }
    }

    // Add cache_control on last message's last block (JackProAi addCacheBreakpoints strategy)
    if (!msgs.empty()) {
        auto& lastMsg = msgs.back();
        if (lastMsg.contains("content") && lastMsg["content"].is_array() && !lastMsg["content"].empty()) {
            lastMsg["content"].back()["cache_control"] = {{"type", "ephemeral"}};
        }
    }

    body["messages"] = std::move(msgs);

    // Tools with cache_control on last tool (creates cache breakpoint)
    if (!config.tools.empty() && config.tools.is_array() && config.tools.size() > 0) {
        nlohmann::json tools = config.tools;
        if (!tools.empty()) {
            tools.back()["cache_control"] = {{"type", "ephemeral"}};
        }
        body["tools"] = std::move(tools);
    }

    // Thinking
    if (config.thinkingEnabled) {
        body["thinking"]["type"] = "enabled";
        body["thinking"]["budget_tokens"] = config.thinkingBudgetTokens;
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
                // Surface streaming progress so the user sees "Generating Write... (32KB)"
                // instead of a frozen spinner for 2-3 minutes during large file writes.
                if (currentToolJson.size() % 4096 < 200) { // ~every 4KB
                    StreamEvent progress;
                    progress.type = StreamEvent::EVT_RETRY; // reuse for progress display
                    progress.retryAttempt = 0; // 0 = not a retry, it's a progress update
                    progress.retryMax = 0;
                    progress.retryDelayMs = (int)currentToolJson.size();
                    progress.content = "generating " + currentToolName;
                    callback(progress);
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
    CurlStreamCtx& curlCtx
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw closecrab::APIError(closecrab::APIErrorType::NETWORK_ERROR, 0, "Failed to initialize CURL");
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("x-api-key: " + apiKey).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, "content-type: application/json");
    // anthropic-beta: matching Claude Code v2.1.152 headers
    headers = curl_slist_append(headers, "anthropic-beta: claude-code-20250219,interleaved-thinking-2025-05-14,prompt-caching-scope-2026-01-05,extended-cache-ttl-2025-04-11,context-management-2025-06-27,structured-outputs-2025-12-15,advanced-tool-use-2025-11-20,tool-search-tool-2025-10-19,redact-thinking-2026-02-12,mid-conversation-system-2026-04-07,mcp-servers-2025-12-04");
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
    std::string proxy = resolveProxy();
    if (!proxy.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
        spdlog::debug("Using proxy: {}", proxy);
    }

    // SSL
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    // Timeouts: 600s overall, 30s connect. No LOW_SPEED (kills thinking pauses).
    // No progress callback (caused ACCESS_VIOLATION segfault).
    // claude-code's 90s idle watchdog uses JS setTimeout, not CURL callbacks.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    // HTTP/1.1 (system libcurl doesn't support HTTP/2)
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

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
    constexpr int FALLBACK_THRESHOLD = 3;
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
            performCurlSSE(url, bodyStr, apiKey_, curlCtx);
            parser.finish();
            return; // Success
        } catch (const APIError& e) {
            if (!isRetryable(e.type)) throw;

            consecutive503++;

            // Model fallback: after N consecutive 503/529, switch to fallback model
            if (consecutive503 >= FALLBACK_THRESHOLD && !fallbackModel_.empty() && activeModel != fallbackModel_) {
                spdlog::warn("Model fallback: {} -> {}", activeModel, fallbackModel_);
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
            spdlog::warn("Retrying in {}ms (attempt {}/{}, model={})...", delayMs, attempt, MAX_RETRIES, activeModel);
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
