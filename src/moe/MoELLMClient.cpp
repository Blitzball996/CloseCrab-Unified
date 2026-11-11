// MoELLMClient.cpp — implements streamChat() using closecrab-moe-engine subprocess.
#include "MoELLMClient.h"
#include "../utils/UUID.h"
#include "../config/Config.h"
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

    // Load tokenizer: prefer HF JSON format (vocab.json + merges.txt from iq_pack.py),
    // fall back to GGUF (may fail on Strata's custom type 42 tensors).
    tokenizer_ = std::make_shared<moe::MoETokenizer>();
    fs::path tokDir = fs::path(modelPath_) / "tokenizer";
    bool tokOk = false;
    if (fs::exists(tokDir / "vocab.json") && fs::exists(tokDir / "merges.txt")) {
        if (tokenizer_->loadFromJson(tokDir.string())) {
            tokOk = true;
            spdlog::info("MoELLMClient: tokenizer loaded from HF JSON ({})", tokDir.string());
        }
    }
    if (!tokOk) {
        static const std::vector<std::string> tokCandidates = {
            "tokenizer/tokenizer.gguf",
            "pack/full/tokenizer/tokenizer.gguf",
            "pack/full/tokenizer.gguf",
            "tokenizer.gguf",
        };
        for (auto& rel : tokCandidates) {
            fs::path p = fs::path(modelPath_) / rel;
            if (fs::exists(p)) {
                if (tokenizer_->load(p.string())) { tokOk = true; break; }
            }
        }
    }
    if (!tokOk)
        spdlog::warn("MoELLMClient: no tokenizer — using byte ids (output may be garbled)");

    // Dynamically calculate max context from free VRAM:
    //   weights + native projections ≈ 3500 MiB
    //   expert cache 128 slots       ≈  350 MiB
    //   system headroom              ≈  500 MiB
    //   KV budget = vramMiB - 4350 MiB
    //   q4_0 KV per token at 48 layers, 8 heads, 128 head_dim ≈ 48 KB/token
    int contextLen = 32768; // conservative default
    if (!hw_.gpus.empty()) {
        double vramMib = hw_.gpus[0].vramGb * 1024.0;
        double kvBudgetMib = vramMib - 4350.0;
        if (kvBudgetMib > 512.0) {
            double tokensFromVram = (kvBudgetMib * 1024.0 * 1024.0) / (24.0 * 1024.0);
            int dynCtx = (int)(tokensFromVram / 1024) * 1024;
            // Cap at 32768: native IQ packs are served with --max-context 32768
            // (q4_0 KV at 32768 + 128 expert cache slots fits in ~11 GB VRAM).
            dynCtx = std::max(4096, std::min(dynCtx, 32768));
            contextLen = dynCtx;
        }
    }
    spdlog::info("MoELLMClient: context window = {} tokens (VRAM-derived)", contextLen);

    engineCfg_.modelPath   = modelPath_;
    engineCfg_.contextLen  = std::clamp(Config::getInstance().getInt("moe.context_len", contextLen), 4096, contextLen);
    engineCfg_.extraArgs = Config::getInstance().getString("moe.engine_args", "");
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
                    // Strata/Qwen's chat template recognizes tool output only
                    // when it is delimited as a tool response, rather than as
                    // ordinary user text.
                    text = "<tool_response>\n";
                    text += blk.toolResult.is_string()
                        ? blk.toolResult.get<std::string>()
                        : blk.toolResult.dump(2);
                    text += "\n</tool_response>";
                }
            }
            role = "user";
        }
        for (const auto& blk : msg.content) {
            if (blk.type == ContentBlockType::TOOL_USE) {
                text += "\n<tool_call>\n<function=" + blk.toolName + ">\n";
                for (const auto& [name, value] : blk.toolInput.items()) {
                    text += "<parameter=" + name + ">\n";
                    text += value.is_string() ? value.get<std::string>() : value.dump();
                    text += "\n</parameter>\n";
                }
                text += "</function>\n</tool_call>";
            }
        }
        if (role == "assistant") text = "<think>\n\n</think>\n\n" + text;
        if (!text.empty())
            prompt += "<|im_start|>" + role + "\n" + text + "\n<|im_end|>\n";
    }
    prompt += "<|im_start|>assistant\n<think>\n\n</think>\n\n";
    return prompt;
}

// ---- tool call parser (mirrors LocalLLMClient) -----------------------------

bool MoELLMClient::parseToolCall(const std::string& text,
                                  std::string& toolName,
                                  std::string& toolUseId,
                                  nlohmann::json& toolInput) const {
    // 1. Qwen native <tool_call>{"name":"...","arguments":{...}}</tool_call>
    auto tcStart = text.find("<tool_call>");
    auto tcEnd   = text.find("</tool_call>");
    if (tcStart != std::string::npos && tcEnd != std::string::npos && tcEnd > tcStart) {
        std::string inner = text.substr(tcStart + 11, tcEnd - tcStart - 11);
        // trim whitespace
        size_t s = inner.find_first_not_of(" \t\r\n");
        if (s != std::string::npos) inner = inner.substr(s);
        if (inner.rfind("<function=", 0) == 0) {
            auto nameEnd = inner.find('>');
            if (nameEnd == std::string::npos) return false;
            toolName = inner.substr(10, nameEnd - 10);
            toolInput = nlohmann::json::object();
            size_t pos = nameEnd + 1;
            while ((pos = inner.find("<parameter=", pos)) != std::string::npos) {
                auto keyEnd = inner.find('>', pos);
                if (keyEnd == std::string::npos) return false;
                auto valueEnd = inner.find("</parameter>", keyEnd);
                if (valueEnd == std::string::npos) return false;
                auto key = inner.substr(pos + 11, keyEnd - pos - 11);
                auto value = inner.substr(keyEnd + 1, valueEnd - keyEnd - 1);
                if (!value.empty() && value.front() == '\n') value.erase(0, 1);
                if (!value.empty() && value.back() == '\n') value.pop_back();
                toolInput[key] = value;
                if (key == "timeout" || key == "offset" || key == "limit" || key == "replace_all") {
                    auto parsed = nlohmann::json::parse(value, nullptr, false);
                    if (!parsed.is_discarded()) toolInput[key] = parsed;
                }
                pos = valueEnd + 12;
            }
            toolUseId = generateUUID();
            return !toolName.empty();
        }
        try {
            auto j = nlohmann::json::parse(inner);
            if (j.contains("name")) {
                toolName  = j["name"].get<std::string>();
                toolInput = j.value("arguments", nlohmann::json::object());
                toolUseId = generateUUID();
                return true;
            }
        } catch (...) {}
    }

    // 2. JSON tool_use block {"tool_use": {"name": "...", "input": {...}}}
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

    // 3. Legacy SKILL: format fallback
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
            if (line.find("PARAMS:") != std::string::npos) {
                inParams = true;
                auto bracePos = line.find('{');
                if (bracePos != std::string::npos) {
                    try {
                        toolInput = nlohmann::json::parse(line.substr(bracePos));
                        break;
                    } catch (...) {}
                }
                continue;
            }
            if (inParams) {
                if (!line.empty() && line.find('{') != std::string::npos) {
                    try { toolInput = nlohmann::json::parse(line); break; } catch (...) {}
                }
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
        // No tokenizer: estimate token count as chars/4 (avg Chinese/English mix).
        // Build a fake ID list of that length so the engine gets a reasonable count.
        // Real content doesn't matter — engine uses the GEN protocol with actual text.
        // Cap at 90% of context to always leave room for output.
        int estimatedTokens = (int)(promptText.size() / 4);
        int maxCtx = engineCfg_.contextLen;
        estimatedTokens = std::min(estimatedTokens, (int)(maxCtx * 0.9));
        // Use token id 1 (BOS equivalent) as placeholder
        promptIds.assign((size_t)estimatedTokens, 1);
        spdlog::warn("MoELLMClient: no tokenizer — estimating {} tokens from {} chars",
                     estimatedTokens, promptText.size());
    }

    if (promptIds.empty()) {
        callback(StreamEvent::error("MoE: tokenization produced empty id list."));
        return;
    }

    // Cap max_new so prompt + response fits in context window
    int maxNew = config.maxTokens > 0 ? std::min(config.maxTokens, 4096) : 4096;
    int available = engineCfg_.contextLen - (int)promptIds.size() - 64; // 64 safety margin
    if (available <= 0) {
        callback(StreamEvent::error("MoE: prompt too long for context window (" +
            std::to_string(promptIds.size()) + " tokens, context=" +
            std::to_string(engineCfg_.contextLen) + ")"));
        return;
    }
    maxNew = std::min(maxNew, available);

    // Accumulate generated ids for detokenization
    std::vector<int32_t> generatedIds;
    generatedIds.reserve(static_cast<size_t>(maxNew));

    // Stream text tokens, but suppress output once a <tool_call> is detected.
    // The full accumulated text is inspected post-generation for tool calls.
    bool toolCallDetected = false;
    size_t lastDecodedLen = 0;

    bool ok = engine_->generate(
        promptIds,
        maxNew,
        config.temperature,
        [&](int32_t tokenId, bool isEos) {
            if (isEos) return;
            generatedIds.push_back(tokenId);
            if (generatedIds.size() % 256 == 0)
                spdlog::info("MoE: generated {} / {} tokens{}", generatedIds.size(),
                             maxNew, toolCallDetected ? " (tool arguments)" : "");

            if (toolCallDetected) return; // suppress text once tool call starts

            if (tokenizer_ && tokenizer_->isLoaded()) {
                std::string full = tokenizer_->decode(generatedIds);
                // Check if a tool call has started — stop streaming text
                if (full.find("<tool_call>") != std::string::npos) {
                    toolCallDetected = true;
                    // Strip the tool_call prefix from already-streamed text
                    // (can't unsend, but at least stop here)
                    return;
                }
                if (full.size() > lastDecodedLen) {
                    std::string chunk = full.substr(lastDecodedLen);
                    lastDecodedLen = full.size();
                    callback(StreamEvent::text(chunk));
                }
            } else {
                if (tokenId >= 32 && tokenId < 127) {
                    callback(StreamEvent::text(std::string(1, static_cast<char>(tokenId))));
                }
            }
        }
    );

    if (!ok) {
        callback(StreamEvent::error("MoE engine generation failed or timed out after " +
            std::to_string(generatedIds.size()) + " tokens."));
        return;
    }

    // Final decode pass to flush any remaining partial state
    if (tokenizer_ && tokenizer_->isLoaded() && !generatedIds.empty() && !toolCallDetected) {
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
        if (toolInput.is_object() && toolInput.contains("file_path") &&
            toolInput["file_path"].is_string()) {
            std::string path = toolInput["file_path"].get<std::string>();
            path.erase(std::remove_if(path.begin(), path.end(), [](char c) {
                return c == '\r' || c == '\n';
            }), path.end());
            toolInput["file_path"] = path;
        }
        StreamEvent te;
        te.type      = StreamEvent::EVT_TOOL_USE;
        te.toolName  = toolName;
        te.toolUseId = toolUseId;
        te.toolInput = toolInput;
        callback(te);
        callback(StreamEvent::stop("tool_use"));
        return;
    }

    if (toolCallDetected) {
        callback(StreamEvent::error("MoE returned an incomplete or invalid tool call after " +
            std::to_string(generatedIds.size()) + " tokens; no tool was executed."));
        return;
    }

    callback(StreamEvent::stop(generatedIds.size() >= static_cast<size_t>(maxNew)
                                  ? "max_tokens" : "end_turn"));
}

} // namespace closecrab
