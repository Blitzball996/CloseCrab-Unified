// MoELLMClient.cpp — implements streamChat() using closecrab-moe-engine subprocess.
// Builds a Qwen chat-format prompt, feeds it to the engine process via
// MoEEngine, and streams decoded tokens back through the StreamCallback.

#include "MoELLMClient.h"
#include "../utils/UUID.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <regex>
#include <sstream>

namespace closecrab {

MoELLMClient::MoELLMClient(const std::string& modelId,
                             const std::string& modelPath)
    : modelId_(modelId), modelPath_(modelPath)
{
    hw_ = moe::detectHardware();
    spdlog::info("MoELLMClient: hardware: {}", hw_.summary);

    engineCfg_.modelPath   = modelPath_;
    engineCfg_.contextLen  = 32768;
    engineCfg_.prefillAuto = true;

    engine_ = std::make_shared<moe::MoEEngine>();
    if (!engine_->start(engineCfg_)) {
        spdlog::error("MoELLMClient: failed to start MoE engine for model '{}'", modelId_);
    }
}

std::vector<moe::ModelEntry> MoELLMClient::availableModels() const {
    return moe::modelCatalog(hw_);
}

int MoELLMClient::countTokens(const std::string& text) const {
    return static_cast<int>(text.size() / kCharsPerToken);
}

// ---- prompt building (Qwen chat format) ------------------------------------
// Matches LocalLLMClient::buildPrompt() exactly so tool-use parsing works.

std::string MoELLMClient::buildPrompt(const std::vector<Message>& messages,
                                       const std::string& systemPrompt) const {
    std::string prompt;
    if (!systemPrompt.empty())
        prompt += "<|im_start|>system\n" + systemPrompt + "\n<|im_end|>\n";

    for (const auto& msg : messages) {
        std::string role = (msg.role == MessageRole::USER) ? "user" : "assistant";
        std::string text = msg.getText();

        if (msg.isToolUseResult) {
            for (const auto& blk : msg.content) {
                if (blk.type == ContentBlockType::TOOL_RESULT) {
                    text = "[Tool Result: " + blk.toolUseId + "]\n";
                    text += blk.toolResult.is_string()
                        ? blk.toolResult.get<std::string>()
                        : blk.toolResult.dump(2);
                }
            }
            role = "user";
        }
        for (const auto& blk : msg.content) {
            if (blk.type == ContentBlockType::TOOL_USE) {
                text += "\nSKILL: " + blk.toolName + "\nPARAMS:\n";
                if (blk.toolInput.is_object()) {
                    for (auto& [k, v] : blk.toolInput.items()) {
                        text += "  " + k + ": ";
                        text += v.is_string() ? v.get<std::string>() : v.dump();
                        text += "\n";
                    }
                } else {
                    text += "  " + blk.toolInput.dump() + "\n";
                }
            }
        }
        if (!text.empty())
            prompt += "<|im_start|>" + role + "\n" + text + "\n<|im_end|>\n";
    }
    prompt += "<|im_start|>assistant\n";
    return prompt;
}

// ---- tool call parser (mirrors LocalLLMClient) -----------------------------

bool MoELLMClient::parseToolCall(const std::string& text,
                                  std::string& toolName,
                                  std::string& toolUseId,
                                  nlohmann::json& toolInput) const {
    // Try JSON tool_use block
    try {
        auto s = text.find('{'), e = text.rfind('}');
        if (s != std::string::npos && e != std::string::npos && e > s) {
            auto j = nlohmann::json::parse(text.substr(s, e - s + 1));
            if (j.contains("tool_use") && j["tool_use"].contains("name")) {
                toolName  = j["tool_use"]["name"];
                toolInput = j["tool_use"].value("input", nlohmann::json::object());
                toolUseId = generateUUID();
                return true;
            }
        }
    } catch (...) {}

    // SKILL: format fallback
    static std::regex skillRe(R"(SKILL:\s*(\w+))");
    std::smatch m;
    if (std::regex_search(text, m, skillRe)) {
        toolName  = m[1].str();
        toolUseId = generateUUID();
        toolInput = nlohmann::json::object();
        static std::regex paramRe(R"(^\s+(\w[\w_-]*):\s*(.+)$)");
        std::istringstream ss(text);
        std::string line;
        bool inParams = false;
        while (std::getline(ss, line)) {
            if (line.find("PARAMS:") != std::string::npos) { inParams = true; continue; }
            if (inParams) {
                std::smatch pm;
                if (std::regex_match(line, pm, paramRe))
                    toolInput[pm[1].str()] = pm[2].str();
            }
        }
        return true;
    }
    return false;
}

// ---- streamChat ------------------------------------------------------------

void MoELLMClient::streamChat(const std::vector<Message>& messages,
                               const std::string& systemPrompt,
                               const ModelConfig& config,
                               StreamCallback callback) {
    if (!engine_ || !engine_->isRunning()) {
        callback(StreamEvent::error("MoE engine is not running. "
                 "Check that the model path is set and the engine binary exists."));
        return;
    }

    std::string prompt = buildPrompt(messages, systemPrompt);

    // Estimate token ids from characters (real tokenization in engine).
    // We send character bytes as a stand-in; the actual engine protocol
    // accepts raw text mode "TEXT_GEN" if no tokenizer is pre-loaded.
    // For now, use a simple whitespace-split byte encoding as prompt ids.
    // A proper BPE tokenizer integration is in the MoETokenizer module.
    std::vector<int32_t> dummyIds;
    dummyIds.reserve(prompt.size() / 4);
    for (unsigned char c : prompt) dummyIds.push_back(static_cast<int32_t>(c));

    std::string accumulated;
    bool hadToolCall = false;
    std::string toolName, toolUseId;
    nlohmann::json toolInput;

    bool ok = engine_->generate(
        dummyIds,
        config.maxTokens > 0 ? config.maxTokens : 4096,
        config.temperature,
        [&](int32_t tokenId, bool isEos) {
            if (isEos) return;
            // Convert raw byte token back to char (byte-level stand-in)
            if (tokenId >= 0 && tokenId < 256) {
                char c = static_cast<char>(tokenId);
                accumulated += c;
                callback(StreamEvent::text(std::string(1, c)));
            }
        }
    );

    if (!ok) {
        callback(StreamEvent::error("MoE engine generation failed."));
        return;
    }

    // Check if accumulated output contains a tool call
    if (parseToolCall(accumulated, toolName, toolUseId, toolInput)) {
        StreamEvent te;
        te.type      = StreamEvent::EVT_TOOL_USE;
        te.toolName  = toolName;
        te.toolUseId = toolUseId;
        te.toolInput = toolInput;
        callback(te);
    }

    callback(StreamEvent::stop("end_turn"));
}

} // namespace closecrab
