#include "OpenAICompatClient.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>

namespace closecrab {

OpenAICompatClient::OpenAICompatClient(const std::string& apiKey,
                                         const std::string& baseUrl,
                                         const std::string& model)
    : apiKey_(apiKey), baseUrl_(baseUrl), model_(model) {}

bool OpenAICompatClient::isLocal() const {
    return baseUrl_.find("127.0.0.1") != std::string::npos ||
           baseUrl_.find("localhost") != std::string::npos;
}

nlohmann::json OpenAICompatClient::buildRequestBody(
    const std::vector<Message>& messages,
    const std::string& systemPrompt,
    const ModelConfig& config
) const {
    nlohmann::json body;
    body["model"] = model_;
    body["max_tokens"] = config.maxTokens;
    body["temperature"] = config.temperature;
    body["stream"] = config.stream;

    // Build messages array in OpenAI format
    nlohmann::json msgs = nlohmann::json::array();

    if (!systemPrompt.empty()) {
        msgs.push_back({{"role", "system"}, {"content", systemPrompt}});
    }

    for (const auto& msg : messages) {
        nlohmann::json m;
        m["role"] = (msg.role == MessageRole::USER) ? "user" : "assistant";

        // Simple text content
        std::string text = msg.getText();
        if (!text.empty()) {
            m["content"] = text;
        } else {
            m["content"] = "";
        }

        // Tool calls (assistant)
        if (msg.role == MessageRole::ASSISTANT) {
            nlohmann::json toolCalls = nlohmann::json::array();
            for (const auto& block : msg.content) {
                if (block.type == ContentBlockType::TOOL_USE) {
                    nlohmann::json tc;
                    tc["id"] = block.toolUseId;
                    tc["type"] = "function";
                    tc["function"]["name"] = block.toolName;
                    tc["function"]["arguments"] = block.toolInput.dump();
                    toolCalls.push_back(std::move(tc));
                }
            }
            if (!toolCalls.empty()) {
                m["tool_calls"] = std::move(toolCalls);
            }
        }

        // Tool results (user)
        if (msg.isToolUseResult) {
            for (const auto& block : msg.content) {
                if (block.type == ContentBlockType::TOOL_RESULT) {
                    m["role"] = "tool";
                    m["tool_call_id"] = block.toolUseId;
                    m["content"] = block.toolResult.is_string()
                        ? block.toolResult.get<std::string>()
                        : block.toolResult.dump();
                }
            }
        }

        msgs.push_back(std::move(m));
    }
    body["messages"] = std::move(msgs);

    // Tools (OpenAI function calling format)
    if (supportsTools_ && !config.tools.empty() && config.tools.is_array()) {
        nlohmann::json tools = nlohmann::json::array();
        for (const auto& t : config.tools) {
            nlohmann::json func;
            func["type"] = "function";
            func["function"]["name"] = t.value("name", "");
            func["function"]["description"] = t.value("description", "");
            func["function"]["parameters"] = t.value("input_schema", nlohmann::json::object());
            tools.push_back(std::move(func));
        }
        body["tools"] = std::move(tools);
    }

    return body;
}

// CURL callback
} // namespace closecrab (close before CURL)

struct OAIStreamCtx {
    closecrab::StreamParser* parser;
};

static size_t oaiCurlCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<OAIStreamCtx*>(userdata);
    ctx->parser->feed(std::string(ptr, size * nmemb));
    return size * nmemb;
}

static void performOAICurl(
    const std::string& url,
    const std::string& bodyStr,
    const std::string& apiKey,
    OAIStreamCtx& curlCtx,
    std::function<void(const closecrab::StreamEvent&)>& callback
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        callback(closecrab::StreamEvent::error("Failed to initialize CURL"));
        return;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + apiKey).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, oaiCurlCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curlCtx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        callback(closecrab::StreamEvent::error(std::string("CURL error: ") + curl_easy_strerror(res)));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

namespace closecrab { // reopen

void OpenAICompatClient::streamChat(
    const std::vector<Message>& messages,
    const std::string& systemPrompt,
    const ModelConfig& config,
    StreamCallback callback
) {
    auto body = buildRequestBody(messages, systemPrompt, config);
    std::string url = baseUrl_;
    // Ensure URL ends with /v1/chat/completions
    if (url.back() == '/') url.pop_back();
    if (url.find("/v1/chat/completions") == std::string::npos) {
        if (url.find("/v1") == std::string::npos) url += "/v1";
        url += "/chat/completions";
    }

    std::string bodyStr = body.dump();

    // Track tool call state across chunks
    std::string currentToolId, currentToolName, currentToolArgs;

    StreamParser parser([&](const StreamParser::SSEEvent& event) {
        if (event.data == "[DONE]") {
            StreamEvent stop;
            stop.type = StreamEvent::EVT_STOP;
            stop.stopReason = currentToolName.empty() ? "end_turn" : "tool_use";

            // Emit accumulated tool call if any
            if (!currentToolName.empty()) {
                StreamEvent toolEvent;
                toolEvent.type = StreamEvent::EVT_TOOL_USE;
                toolEvent.toolName = currentToolName;
                toolEvent.toolUseId = currentToolId;
                try {
                    toolEvent.toolInput = nlohmann::json::parse(currentToolArgs);
                } catch (...) {
                    toolEvent.toolInput = nlohmann::json::object();
                }
                callback(toolEvent);
                currentToolName.clear();
            }

            callback(stop);
            return;
        }

        try {
            auto j = nlohmann::json::parse(event.data);
            if (!j.contains("choices") || j["choices"].empty()) return;

            auto& choice = j["choices"][0];
            auto delta = choice.value("delta", nlohmann::json::object());

            // Text content
            if (delta.contains("content") && !delta["content"].is_null()) {
                std::string text = delta["content"].get<std::string>();
                if (!text.empty()) {
                    callback({StreamEvent::EVT_TEXT, text});
                }
            }

            // Tool calls
            if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                for (const auto& tc : delta["tool_calls"]) {
                    if (tc.contains("id")) currentToolId = tc["id"].get<std::string>();
                    if (tc.contains("function")) {
                        if (tc["function"].contains("name"))
                            currentToolName = tc["function"]["name"].get<std::string>();
                        if (tc["function"].contains("arguments"))
                            currentToolArgs += tc["function"]["arguments"].get<std::string>();
                    }
                }
            }

            // Usage
            if (j.contains("usage")) {
                StreamEvent usageEvent;
                usageEvent.type = StreamEvent::EVT_USAGE_UPDATE;
                usageEvent.usage.inputTokens = j["usage"].value("prompt_tokens", 0);
                usageEvent.usage.outputTokens = j["usage"].value("completion_tokens", 0);
                callback(usageEvent);
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to parse OpenAI SSE: {}", e.what());
        }
    });

    OAIStreamCtx curlCtx{&parser};
    performOAICurl(url, bodyStr, apiKey_, curlCtx, callback);
    parser.finish();
}

} // namespace closecrab
