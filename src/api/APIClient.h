#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
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
        EVT_USAGE_UPDATE    // Token usage update
    };

    Type type = EVT_TEXT;
    std::string content;        // TEXT / THINKING / ERROR
    std::string toolName;       // TOOL_USE
    std::string toolUseId;      // TOOL_USE
    nlohmann::json toolInput;   // TOOL_USE
    std::string stopReason;     // STOP: "end_turn", "tool_use", "max_tokens"
    TokenUsage usage;           // USAGE_UPDATE

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

    // Tool use
    nlohmann::json tools = nlohmann::json::array();
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
