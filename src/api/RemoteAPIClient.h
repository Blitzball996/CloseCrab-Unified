#pragma once

#include "APIClient.h"
#include "StreamParser.h"
#include <string>

namespace closecrab {

// Anthropic Claude API client (/v1/messages with SSE streaming)
class RemoteAPIClient : public APIClient {
public:
    RemoteAPIClient(const std::string& apiKey,
                    const std::string& baseUrl = "https://api.anthropic.com",
                    const std::string& model = "claude-opus-4-7");

    void streamChat(
        const std::vector<Message>& messages,
        const std::string& systemPrompt,
        const ModelConfig& config,
        StreamCallback callback
    ) override;

    int countTokens(const std::string& text) const override;
    std::string getModelName() const override { return model_; }
    bool isLocal() const override { return false; }
    bool supportsToolUse() const override { return true; }
    bool supportsThinking() const override { return true; }

    void setFallbackModel(const std::string& model) { fallbackModel_ = model; }

private:
    nlohmann::json buildRequestBody(const std::vector<Message>& messages,
                                     const std::string& systemPrompt,
                                     const ModelConfig& config,
                                     const std::string& modelOverride = "") const;
    void handleSSEEvent(const StreamParser::SSEEvent& event, StreamCallback& callback,
                        std::string& currentToolName, std::string& currentToolId,
                        std::string& currentToolJson) const;

    std::string apiKey_;
    std::string baseUrl_;
    std::string model_;
    std::string fallbackModel_;
};

} // namespace closecrab
