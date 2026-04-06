#pragma once

#include "APIClient.h"
#include "StreamParser.h"
#include <string>

namespace closecrab {

// OpenAI-compatible API client (/v1/chat/completions)
// Supports: LM Studio, SiliconFlow, vLLM, Ollama, etc.
class OpenAICompatClient : public APIClient {
public:
    OpenAICompatClient(const std::string& apiKey,
                       const std::string& baseUrl,
                       const std::string& model);

    void streamChat(
        const std::vector<Message>& messages,
        const std::string& systemPrompt,
        const ModelConfig& config,
        StreamCallback callback
    ) override;

    std::string getModelName() const override { return model_; }
    bool isLocal() const override;
    bool supportsToolUse() const override { return supportsTools_; }
    bool supportsThinking() const override { return false; }

    void setSupportsTools(bool v) { supportsTools_ = v; }

private:
    nlohmann::json buildRequestBody(const std::vector<Message>& messages,
                                     const std::string& systemPrompt,
                                     const ModelConfig& config) const;

    std::string apiKey_;
    std::string baseUrl_;
    std::string model_;
    bool supportsTools_ = false;
};

} // namespace closecrab
