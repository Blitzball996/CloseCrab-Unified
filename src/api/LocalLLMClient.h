#pragma once

#include "APIClient.h"
#include "../llm/LLMEngine.h"
#include <memory>

namespace closecrab {

// Wraps existing CloseCrab LLMEngine as an APIClient
class LocalLLMClient : public APIClient {
public:
    explicit LocalLLMClient(LLMEngine* engine);

    void streamChat(
        const std::vector<Message>& messages,
        const std::string& systemPrompt,
        const ModelConfig& config,
        StreamCallback callback
    ) override;

    int countTokens(const std::string& text) const override;
    std::string getModelName() const override;
    bool isLocal() const override { return true; }
    bool supportsToolUse() const override;
    bool supportsThinking() const override { return false; }

private:
    std::string buildPrompt(const std::vector<Message>& messages,
                            const std::string& systemPrompt) const;
    // Parse tool calls from raw LLM output (SKILL: format or JSON)
    bool parseToolCall(const std::string& text, std::string& toolName,
                       std::string& toolUseId, nlohmann::json& toolInput) const;

    LLMEngine* engine_;  // Non-owning pointer
};

} // namespace closecrab
