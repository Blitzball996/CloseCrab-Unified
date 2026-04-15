#include "RemoteAPIClient.h"
#include "APIError.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <fstream>

namespace closecrab {

RemoteAPIClient::RemoteAPIClient(const std::string& apiKey,
                                   const std::string& baseUrl,
                                   const std::string& model)
    : apiKey_(apiKey), baseUrl_(baseUrl), model_(model) {}

nlohmann::json RemoteAPIClient::buildRequestBody(
    const std::vector<Message>& messages,
    const std::string& systemPrompt,
    const ModelConfig& config
) const {
    nlohmann::json body;
    body["model"] = model_;
    body["max_tokens"] = config.maxTokens;
    body["stream"] = config.stream;

    if (config.temperature >= 0 && config.tools.empty()) body["temperature"] = config.temperature;

    // System prompt — use array format with cache_control for prompt caching
    if (!systemPrompt.empty()) {
        body["system"] = nlohmann::json::array({
            {{"type", "text"}, {"text", systemPrompt},
             {"cache_control", {{"type", "ephemeral"}}}}
        });
    }

    // Messages
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& msg : messages) {
        msgs.push_back(msg.toApiJson());
    }
    body["messages"] = std::move(msgs);

    // Tools — add cache_control on last tool for prompt caching
    if (!config.tools.empty() && config.tools.is_array() && config.tools.size() > 0) {
        nlohmann::json tools = config.tools;
        // Add cache_control to last tool to enable caching of entire tools block
        if (!tools.empty()) {
            tools.back()["cache_control"] = {{"type", "ephemeral"}};
        }
        body["tools"] = tools;
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
    headers = curl_slist_append(headers, "anthropic-beta: prompt-caching-2024-07-31,prompt-caching-scope-2026-01-05,token-efficient-tools-2026-03-28");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)bodyStr.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlStreamCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curlCtx);

    // SSL
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    // Timeouts
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);       // 5 min overall timeout
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
        // Network errors are retryable
        throw closecrab::APIError(closecrab::APIErrorType::NETWORK_ERROR,
                                   static_cast<int>(httpCode), errMsg);
    }

    if (httpCode >= 400) {
        auto errType = closecrab::classifyHttpStatus(httpCode);
        std::string errBody = curlCtx.rawResponse.substr(0, 500);
        spdlog::error("HTTP {} response body: {}", httpCode, errBody);
        throw closecrab::APIError(errType, static_cast<int>(httpCode),
                                   "HTTP " + std::to_string(httpCode) + ": " + errBody);
    }
}

namespace closecrab { // reopen

void RemoteAPIClient::streamChat(
    const std::vector<Message>& messages,
    const std::string& systemPrompt,
    const ModelConfig& config,
    StreamCallback callback
) {
    // Fast-fail: no API key means no valid provider configured
    if (apiKey_.empty()) {
        throw APIError(APIErrorType::AUTH_ERROR, 0,
            "No API key configured. Use --api-key or set ANTHROPIC_AUTH_TOKEN.");
    }

    auto body = buildRequestBody(messages, systemPrompt, config);
    std::string url = baseUrl_ + "/v1/messages";
    std::string bodyStr = body.dump();

    spdlog::info("API request: {} bytes, {} tools, system_prompt={} chars, url={}",
                 bodyStr.size(),
                 config.tools.is_array() ? config.tools.size() : 0,
                 systemPrompt.size(), url);

    // Dump full request body to file for debugging
    {
        std::ofstream dbg("G:/temp/last_request.json");
        if (dbg.is_open()) { dbg << body.dump(2); dbg.close(); }
    }

    // Debug: dump first 500 chars of body
    spdlog::debug("Request body (first 500): {}", bodyStr.substr(0, 500));

    withRetry([&]() {
        std::string currentToolName, currentToolId, currentToolJson;

        StreamParser parser([&](const StreamParser::SSEEvent& event) {
            handleSSEEvent(event, callback, currentToolName, currentToolId, currentToolJson);
        });

        CurlStreamCtx curlCtx{&parser};
        try {
            performCurlSSE(url, bodyStr, apiKey_, curlCtx);
        } catch (const APIError& e) {
            // Re-throw all errors — let withRetry handle retries with full tools
            throw;
        }
        parser.finish();
    }, 3);
}

int RemoteAPIClient::countTokens(const std::string& text) const {
    // Rough estimation: ~4 chars per token for English, ~2 for CJK
    return static_cast<int>(text.size() / 3);
}

} // namespace closecrab
