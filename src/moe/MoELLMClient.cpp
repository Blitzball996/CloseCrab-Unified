// MoELLMClient.cpp — implements streamChat() using closecrab-moe-engine subprocess.
#include "MoELLMClient.h"
#include "../utils/UUID.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace closecrab {

// Find tokenizer GGUF in the pack directory.
// Strata pack layout: pack/full/tokenizer/tokenizer.gguf  or  pack/full/tokenizer.gguf
// or any .gguf in the root that contains vocab.
static std::string findTokenizerGguf(const std::string& packDir) {
    // Common locations inside a Qwen/Strata pack:
    static const std::vector<std::string> candidates = {
        "pack/full/tokenizer/tokenizer.gguf",
        "pack/full/tokenizer.gguf",
        "tokenizer.gguf",
        "pack/tokenizer/tokenizer.gguf",
    };
    for (auto& rel : candidates) {
        fs::path p = fs::path(packDir) / rel;
        if (fs::exists(p)) return p.string();
    }
    // Fallback: any .gguf file in root (first model shard also carries vocab)
    try {
        for (auto& entry : fs::directory_iterator(packDir)) {
            if (entry.path().extension() == ".gguf")
                return entry.path().string();
        }
    } catch (...) {}
    return "";
}

MoELLMClient::MoELLMClient(const std::string& modelId,
                             const std::string& modelPath)
    : modelId_(modelId), modelPath_(modelPath)
{
    hw_ = moe::detectHardware();
    spdlog::info("MoELLMClient: hardware: {}", hw_.summary);

    // Load tokenizer from pack directory
    tokenizer_ = std::make_shared<moe::MoETokenizer>();
    std::string tokGguf = findTokenizerGguf(modelPath_);
    if (!tokGguf.empty()) {
        if (!tokenizer_->load(tokGguf))
            spdlog::warn("MoELLMClient: tokenizer load failed, will use char estimate");
    } else {
        spdlog::warn("MoELLMClient: no tokenizer.gguf found in {}, using char estimate",
                     modelPath_);
    }

    engineCfg_.modelPath   = modelPath_;
    engineCfg_.contextLen  = 32768;
    engineCfg_.prefillAuto = true;

    engine_ = std::make_shared<moe::MoEEngine>();
    if (!engine_->start(engineCfg_))
        spdlog::error("MoELLMClient: failed to start engine for '{}'", modelId_);
}

std::vector<moe::ModelEntry> MoELLMClient::availableModels() const {
    return moe::modelCatalog(hw_);
}

int MoELLMClient::countTokens(const std::string& text) const {
    if (tokenizer_ && tokenizer_->isLoaded()) {
        auto ids = tokenizer_->encode(text, false);
        return static_cast<int>(ids.size());
    }
    return static_cast<int>(text.size() / 4);
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

    // Build Qwen chat prompt string
    std::string promptText = buildPrompt(messages, systemPrompt);

    // Tokenize prompt text -> ids
    std::vector<int32_t> promptIds;
    if (tokenizer_ && tokenizer_->isLoaded()) {
        promptIds = tokenizer_->encode(promptText, false);
        spdlog::debug("MoELLMClient: prompt {} chars -> {} tokens",
                      promptText.size(), promptIds.size());
    } else {
        // Fallback: raw bytes as token ids (ASCII-safe subset only)
        promptIds.reserve(promptText.size());
        for (unsigned char c : promptText)
            promptIds.push_back(static_cast<int32_t>(c));
        spdlog::warn("MoELLMClient: no tokenizer — using byte ids (output may be garbled)");
    }

    if (promptIds.empty()) {
        callback(StreamEvent::error("MoE: tokenization produced empty id list."));
        return;
    }

    // Accumulate generated ids for detokenization
    std::vector<int32_t> generatedIds;
    generatedIds.reserve(static_cast<size_t>(config.maxTokens > 0 ? config.maxTokens : 4096));

    // Pending partial UTF-8: detokenize incrementally, stream text chunks.
    // Strategy: decode the full generated sequence each time (avoids split UTF-8
    // multibyte chars) but only stream the new suffix since last decode.
    size_t lastDecodedLen = 0;

    bool ok = engine_->generate(
        promptIds,
        config.maxTokens > 0 ? config.maxTokens : 4096,
        config.temperature,
        [&](int32_t tokenId, bool isEos) {
            if (isEos) return;
            generatedIds.push_back(tokenId);

            if (tokenizer_ && tokenizer_->isLoaded()) {
                // Decode full sequence, stream only the new suffix
                std::string full = tokenizer_->decode(generatedIds);
                if (full.size() > lastDecodedLen) {
                    std::string chunk = full.substr(lastDecodedLen);
                    lastDecodedLen = full.size();
                    callback(StreamEvent::text(chunk));
                }
            } else {
                // Byte fallback: emit char directly
                if (tokenId >= 32 && tokenId < 127) {
                    callback(StreamEvent::text(std::string(1, static_cast<char>(tokenId))));
                }
            }
        }
    );

    if (!ok && generatedIds.empty()) {
        callback(StreamEvent::error("MoE engine generation failed."));
        return;
    }

    // Final decode pass to flush any remaining partial state
    if (tokenizer_ && tokenizer_->isLoaded() && !generatedIds.empty()) {
        std::string full = tokenizer_->decode(generatedIds);
        if (full.size() > lastDecodedLen)
            callback(StreamEvent::text(full.substr(lastDecodedLen)));
    }

    // Check accumulated output for tool calls
    std::string accumulated;
    if (tokenizer_ && tokenizer_->isLoaded())
        accumulated = tokenizer_->decode(generatedIds);
    else
        for (int32_t id : generatedIds)
            if (id >= 32 && id < 127) accumulated += static_cast<char>(id);

    std::string toolName, toolUseId;
    nlohmann::json toolInput;
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
