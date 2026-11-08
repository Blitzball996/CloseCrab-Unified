#pragma once
// MoELLMClient.h — APIClient using the MoE local engine subprocess.

#include "../api/APIClient.h"
#include "MoEEngine.h"
#include "MoEHardwareProbe.h"
#include "MoETokenizer.h"
#include <memory>
#include <string>

namespace closecrab {

class MoELLMClient : public APIClient {
public:
    MoELLMClient(const std::string& modelId, const std::string& modelPath);
    ~MoELLMClient() override = default;

    void streamChat(const std::vector<Message>& messages,
                    const std::string& systemPrompt,
                    const ModelConfig& config,
                    StreamCallback callback) override;

    int countTokens(const std::string& text) const override;

    std::string getModelName() const override { return "local-moe:" + modelId_; }
    bool isLocal()          const override { return true; }
    bool supportsToolUse()  const override { return true; }
    bool supportsThinking() const override { return false; }

    const moe::HardwareInfo& hardwareInfo() const { return hw_; }
    std::vector<moe::ModelEntry> availableModels() const;

private:
    std::string buildPrompt(const std::vector<Message>& messages,
                            const std::string& systemPrompt) const;
    bool parseToolCall(const std::string& text, std::string& toolName,
                       std::string& toolUseId, nlohmann::json& toolInput) const;

    std::string                     modelId_;
    std::string                     modelPath_;
    moe::HardwareInfo               hw_;
    moe::EngineConfig               engineCfg_;
    std::shared_ptr<moe::MoEEngine>     engine_;
    std::shared_ptr<moe::MoETokenizer>  tokenizer_;
};

} // namespace closecrab
