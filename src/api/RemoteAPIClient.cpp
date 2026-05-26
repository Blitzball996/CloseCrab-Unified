#include "RemoteAPIClient.h"
#include "APIError.h"
#include "../config/Config.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstdlib>
#include <mutex>
#include <condition_variable>

// Global API request semaphore: limits concurrent requests to avoid overwhelming proxy
static std::mutex g_apiMutex;
static std::condition_variable g_apiCv;
static int g_activeRequests = 0;
static constexpr int MAX_CONCURRENT_REQUESTS = 1;

struct APIRequestGuard {
    APIRequestGuard() {
        std::unique_lock<std::mutex> lock(g_apiMutex);
        g_apiCv.wait(lock, [] { return g_activeRequests < MAX_CONCURRENT_REQUESTS; });
        g_activeRequests++;
    }
    ~APIRequestGuard() {
        std::lock_guard<std::mutex> lock(g_apiMutex);
        g_activeRequests--;
        g_apiCv.notify_one();
    }
};

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

    // System prompt
    if (!systemPrompt.empty()) {
        body["system"] = nlohmann::json::array({
            {{"type", "text"}, {"text", systemPrompt}}
        });
    }

    // Messages
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& msg : messages) {
        msgs.push_back(msg.toApiJson());
    }

    // API Microcompact: if messages are too large, clear old tool_result content
    // (like JackProAi's apiMicrocompact - clears Bash/Glob/Grep/Read results)
    constexpr size_t MAX_MESSAGES_SIZE = 8000;
    std::string msgsStr = msgs.dump();
    if (msgsStr.size() > MAX_MESSAGES_SIZE && msgs.size() > 2) {
        // Clear tool_result content from all but the last 2 messages
        for (size_t i = 0; i + 2 < msgs.size(); i++) {
            if (msgs[i].value("role", "") == "user" && msgs[i].contains("content") && msgs[i]["content"].is_array()) {
                for (auto& block : msgs[i]["content"]) {
                    if (block.value("type", "") == "tool_result") {
                        if (block.contains("content") && block["content"].is_string()) {
                            std::string content = block["content"].get<std::string>();
                            if (content.size() > 200) {
                                block["content"] = content.substr(0, 150) + "\n[cleared for context limit]";
                            }
                        } else if (block.contains("content") && block["content"].is_array()) {
                            for (auto& sub : block["content"]) {
                                if (sub.value("type", "") == "text") {
                                    std::string text = sub.value("text", "");
                                    if (text.size() > 200) {
                                        sub["text"] = text.substr(0, 150) + "\n[cleared for context limit]";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    body["messages"] = std::move(msgs);

    // Tools
    if (!config.tools.empty() && config.tools.is_array() && config.tools.size() > 0) {
        body["tools"] = config.tools;
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
    std::string chunk(ptr, totalSize);
    ctx->parser->feed(chunk);
    // Also accumulate raw response for error diagnostics
    ctx->rawResponse += chunk;
    return totalSize;
}

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
    headers = curl_slist_append(headers, "accept: text/event-stream");

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

    // Timeouts
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
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
        spdlog::debug("HTTP {} response body: {}", httpCode, errBody);
        throw closecrab::APIError(errType, static_cast<int>(httpCode),
                                   "HTTP " + std::to_string(httpCode));
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

    std::string url = baseUrl_ + "/v1/messages";
    constexpr int MAX_RETRIES = 10;
    constexpr int FALLBACK_THRESHOLD = 3;
    int consecutive503 = 0;
    std::string activeModel = model_;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        auto body = buildRequestBody(messages, systemPrompt, config, activeModel);
        std::string bodyStr = body.dump();

        if (attempt == 1) {
            spdlog::info("API request: {} bytes, {} tools, system_prompt={} chars, model={}, url={}",
                         bodyStr.size(),
                         config.tools.is_array() ? config.tools.size() : 0,
                         systemPrompt.size(), activeModel, url);
        }

        if (const char* debugDir = std::getenv("CLOSECRAB_DEBUG_DIR")) {
            std::string debugPath = std::string(debugDir) + "/last_request.json";
            std::ofstream dbg(debugPath);
            if (dbg.is_open()) { dbg << body.dump(2); dbg.close(); }
        }

        try {
            std::string currentToolName, currentToolId, currentToolJson;
            StreamParser parser([&](const StreamParser::SSEEvent& event) {
                handleSSEEvent(event, callback, currentToolName, currentToolId, currentToolJson);
            });
            CurlStreamCtx curlCtx{&parser};
            {
                APIRequestGuard guard; // Serialize API requests
                performCurlSSE(url, bodyStr, apiKey_, curlCtx);
            }
            parser.finish();
            return; // Success
        } catch (const APIError& e) {
            if (!isRetryable(e.type)) throw;

            consecutive503++;

            // Model fallback: after N consecutive 503/529, switch to fallback model
            if (consecutive503 >= FALLBACK_THRESHOLD && !fallbackModel_.empty() && activeModel != fallbackModel_) {
                spdlog::info("Model fallback triggered after {} consecutive errors: {} -> {}",
                             consecutive503, activeModel, fallbackModel_);
                activeModel = fallbackModel_;
                consecutive503 = 0;
            }

            if (attempt >= MAX_RETRIES) throw;

            // Exponential backoff with jitter: 3s, 6s, 12s, 24s, 32s...
            int baseDelay = 3000 * (1 << (attempt - 1));
            if (baseDelay > 32000) baseDelay = 32000;
            int jitter = rand() % (baseDelay / 4 + 1);
            int delayMs = baseDelay + jitter;

            // Silent for first 3 attempts (like JackProAi), then info level
            if (attempt <= 3) {
                spdlog::debug("Retry attempt {}/{}, model={}, waiting {}ms",
                              attempt, MAX_RETRIES, activeModel, delayMs);
            } else {
                spdlog::info("Retrying... (attempt {}/{}), model={}",
                             attempt, MAX_RETRIES, activeModel);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }
}

int RemoteAPIClient::countTokens(const std::string& text) const {
    return static_cast<int>(text.size() / 3);
}

} // namespace closecrab
