#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <nlohmann/json.hpp>
#include "../core/Message.h"

namespace closecrab {

// ============================================================
// Stream events from API (对标 JackProAi StreamEvent)
// ============================================================

struct StreamEvent {
    enum Type {
        EVT_TEXT,           // Text content delta
        EVT_TOOL_USE,       // Tool call request
        EVT_THINKING,       // Extended thinking content
        EVT_STOP,           // Generation stopped
        EVT_ERROR,          // Error occurred
        EVT_USAGE_UPDATE,   // Token usage update
        EVT_RETRY,          // Retryable failure — about to retry (surfaced to UI)
        EVT_WEB_SEARCH_RESULT  // Server-side web_search_tool_result: hits in `toolInput` (array of {title,url})
    };

    Type type = EVT_TEXT;
    std::string content;        // TEXT / THINKING / ERROR / RETRY (reason)
    std::string toolName;       // TOOL_USE
    std::string toolUseId;      // TOOL_USE
    nlohmann::json toolInput;   // TOOL_USE
    std::string stopReason;     // STOP: "end_turn", "tool_use", "max_tokens"
    TokenUsage usage;           // USAGE_UPDATE
    int retryAttempt = 0;       // RETRY: 1-based attempt number
    int retryMax = 0;           // RETRY: max attempts
    int retryDelayMs = 0;       // RETRY: delay before next attempt

    // Convenience constructors
    StreamEvent() = default;
    StreamEvent(Type t, const std::string& c) : type(t), content(c) {}
    static StreamEvent text(const std::string& c) { StreamEvent e; e.type = EVT_TEXT; e.content = c; return e; }
    static StreamEvent error(const std::string& c) { StreamEvent e; e.type = EVT_ERROR; e.content = c; return e; }
    static StreamEvent thinking(const std::string& c) { StreamEvent e; e.type = EVT_THINKING; e.content = c; return e; }
    static StreamEvent stop(const std::string& reason) { StreamEvent e; e.type = EVT_STOP; e.stopReason = reason; return e; }
};

using StreamCallback = std::function<void(const StreamEvent&)>;

// ============================================================
// Model configuration
// ============================================================

struct ModelConfig {
    int maxTokens = 4096;
    float temperature = 0.7f;
    float topP = 1.0f;
    bool stream = true;

    // Thinking mode
    bool thinkingEnabled = false;
    int thinkingBudgetTokens = 10000;

    // Reasoning effort (API-native output_config.effort — Claude Code 2.1.x).
    // One of: "low" | "medium" | "high" | "xhigh" | "max". Empty = don't send the
    // field (lets the server pick its model default). This is the NEW mechanism
    // that supersedes thinkingBudgetTokens on effort-capable models (opus-4-6+,
    // sonnet-4-6+, opus-4-8). Sent as output_config.effort + the effort beta
    // header. See RemoteAPIClient::buildRequestBody.
    std::string effort = "";

    // Tool use
    nlohmann::json tools = nlohmann::json::array();

    // Server-side tools (e.g. web_search_20250305). Injected VERBATIM into the
    // request body's tools array alongside client tools. Used by WebSearchTool to
    // run the search on the SAME robust API path as the main loop (HTTP/1.1, UA,
    // SSL, 10x retry + fallback, proxy) — JackProAi alignment. When non-empty,
    // RemoteAPIClient also adds the web-search-2025-03-05 beta header.
    nlohmann::json extraServerTools = nlohmann::json::array();

    // P2 fork cache sharing (JackProAi skipCacheWrite). When true (sub-agent /
    // fire-and-forget fork), the message-level cache_control marker is placed on
    // the SECOND-to-last message instead of the last, so the fork READS the
    // parent's cached prefix but does not WRITE its own tail into the cache.
    bool skipCacheWrite = false;

    // Esc/abort signal: when set true mid-stream, the curl transfer aborts
    // immediately (CURLOPT_XFERINFOFUNCTION). Points at QueryEngine::interrupted_.
    // const because we only ever READ it on the API side.
    const std::atomic<bool>* abortFlag = nullptr;
};

// ============================================================
// API Client interface (对标 JackProAi API Service)
// 3 implementations: LocalLLM, RemoteAPI (Anthropic), OpenAICompat
// ============================================================

class APIClient {
public:
    virtual ~APIClient() = default;

    // Stream a chat completion
    virtual void streamChat(
        const std::vector<Message>& messages,
        const std::string& systemPrompt,
        const ModelConfig& config,
        StreamCallback callback
    ) = 0;

    // Non-streaming (convenience, default impl uses streamChat)
    virtual std::string chat(
        const std::vector<Message>& messages,
        const std::string& systemPrompt,
        const ModelConfig& config
    );

    // Count tokens in text (approximate)
    virtual int countTokens(const std::string& text) const { return static_cast<int>(text.size() / 4); }

    // Get model info
    virtual std::string getModelName() const = 0;
    virtual bool isLocal() const = 0;
    virtual bool supportsToolUse() const { return true; }
    virtual bool supportsThinking() const { return false; }
    virtual bool supportsStreaming() const { return true; }
};

// Default non-streaming implementation
inline std::string APIClient::chat(
    const std::vector<Message>& messages,
    const std::string& systemPrompt,
    const ModelConfig& config
) {
    std::string result;
    ModelConfig cfg = config;
    cfg.stream = false;

    streamChat(messages, systemPrompt, cfg, [&](const StreamEvent& event) {
        if (event.type == StreamEvent::EVT_TEXT) {
            result += event.content;
        }
    });
    return result;
}

} // namespace closecrab
