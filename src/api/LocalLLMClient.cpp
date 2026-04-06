#include "LocalLLMClient.h"
#include "../utils/UUID.h"
#include <spdlog/spdlog.h>
#include <regex>

namespace closecrab {

LocalLLMClient::LocalLLMClient(LLMEngine* engine) : engine_(engine) {}

std::string LocalLLMClient::buildPrompt(const std::vector<Message>& messages,
                                         const std::string& systemPrompt) const {
    // Qwen chat format (same as CloseCrab original)
    std::string prompt;

    if (!systemPrompt.empty()) {
        prompt += "<|im_start|>system\n" + systemPrompt + "\n<|im_end|>\n";
    }

    for (const auto& msg : messages) {
        std::string role = (msg.role == MessageRole::USER) ? "user" : "assistant";
        std::string text = msg.getText();

        // For tool results, format them as user messages
        if (msg.isToolUseResult) {
            for (const auto& block : msg.content) {
                if (block.type == ContentBlockType::TOOL_RESULT) {
                    text = "[Tool Result: " + block.toolUseId + "]\n";
                    text += block.toolResult.is_string()
                        ? block.toolResult.get<std::string>()
                        : block.toolResult.dump(2);
                }
            }
            role = "user";
        }

        // For tool use blocks, format them in assistant message
        for (const auto& block : msg.content) {
            if (block.type == ContentBlockType::TOOL_USE) {
                text += "\nSKILL: " + block.toolName + "\n";
                text += "PARAMS: " + block.toolInput.dump() + "\n";
            }
        }

        if (!text.empty()) {
            prompt += "<|im_start|>" + role + "\n" + text + "\n<|im_end|>\n";
        }
    }

    prompt += "<|im_start|>assistant\n";
    return prompt;
}

bool LocalLLMClient::parseToolCall(const std::string& text, std::string& toolName,
                                    std::string& toolUseId, nlohmann::json& toolInput) const {
    // Try JSON tool_use format first
    try {
        auto j = nlohmann::json::parse(text);
        if (j.contains("tool_use") && j["tool_use"].contains("name")) {
            toolName = j["tool_use"]["name"].get<std::string>();
            toolUseId = generateUUID();
            toolInput = j["tool_use"].value("input", nlohmann::json::object());
            return true;
        }
    } catch (...) {}

    // Try SKILL: format (CloseCrab original)
    // Match across multiple lines with [\s\S]
    std::regex skillRegex(R"(SKILL:\s*(\w+)\s*\nPARAMS:\s*([\s\S]*))");
    std::smatch match;
    if (std::regex_search(text, match, skillRegex)) {
        toolName = match[1].str();
        toolUseId = generateUUID();

        // Parse PARAMS: key=value, key=value
        // Strategy: split on ", key=" boundaries (not just commas)
        // to handle values that contain commas
        std::string paramsStr = match[2].str();
        // Trim trailing whitespace/newlines
        while (!paramsStr.empty() && (paramsStr.back() == '\n' || paramsStr.back() == '\r' || paramsStr.back() == ' '))
            paramsStr.pop_back();

        toolInput = nlohmann::json::object();

        // Find all "key=" positions
        std::regex keyRegex(R"((\w+)\s*=)");
        std::vector<std::pair<std::string, size_t>> keys; // key name, position after '='
        auto kbegin = std::sregex_iterator(paramsStr.begin(), paramsStr.end(), keyRegex);
        auto kend = std::sregex_iterator();
        for (auto it = kbegin; it != kend; ++it) {
            size_t valStart = it->position() + it->length();
            keys.push_back({(*it)[1].str(), valStart});
        }

        // Extract values: each value runs from after '=' to the start of next ", key="
        for (size_t i = 0; i < keys.size(); i++) {
            std::string key = keys[i].first;
            size_t valStart = keys[i].second;
            size_t valEnd;
            if (i + 1 < keys.size()) {
                // Find the ", " before next key
                valEnd = paramsStr.rfind(',', keys[i + 1].second);
                if (valEnd == std::string::npos || valEnd < valStart) valEnd = keys[i + 1].second;
            } else {
                valEnd = paramsStr.size();
            }
            std::string val = paramsStr.substr(valStart, valEnd - valStart);
            // Trim
            while (!val.empty() && (val.front() == ' ' || val.front() == ',')) val.erase(val.begin());
            while (!val.empty() && (val.back() == ' ' || val.back() == ',' || val.back() == '\n' || val.back() == '\r')) val.pop_back();
            toolInput[key] = val;
        }

        return true;
    }

    return false;
}

void LocalLLMClient::streamChat(
    const std::vector<Message>& messages,
    const std::string& systemPrompt,
    const ModelConfig& config,
    StreamCallback callback
) {
    if (!engine_ || !engine_->isLoaded()) {
        callback({StreamEvent::EVT_ERROR, "LLM engine not loaded"});
        return;
    }

    std::string fullPrompt = buildPrompt(messages, systemPrompt);
    std::string accumulated;

    engine_->generateRaw(fullPrompt, config.maxTokens, config.temperature,
        // onToken callback
        [&](const std::string& token) {
            // Filter out special tokens that leak from the model
            if (token.find("<|im_start|>") != std::string::npos ||
                token.find("<|im_end|>") != std::string::npos ||
                token.find("<|im_sep|>") != std::string::npos ||
                token.find("<|endoftext|>") != std::string::npos) {
                return; // Skip special tokens
            }
            accumulated += token;
            callback({StreamEvent::EVT_TEXT, token});
        },
        // onComplete callback
        [&]() {
            // Clean up special tokens from accumulated text
            std::string clean = accumulated;
            for (const auto& tag : {"<|im_start|>", "<|im_end|>", "<|im_sep|>",
                                     "<|endoftext|>", "assistant\n", "user\n"}) {
                size_t pos;
                while ((pos = clean.find(tag)) != std::string::npos) {
                    clean.erase(pos, std::string(tag).size());
                }
            }
            // Trim
            while (!clean.empty() && (clean.front() == '\n' || clean.front() == ' ')) clean.erase(clean.begin());
            while (!clean.empty() && (clean.back() == '\n' || clean.back() == ' ')) clean.pop_back();

            // Check if the cleaned text contains a tool call
            std::string toolName, toolUseId;
            nlohmann::json toolInput;
            if (parseToolCall(clean, toolName, toolUseId, toolInput)) {
                StreamEvent toolEvent;
                toolEvent.type = StreamEvent::EVT_TOOL_USE;
                toolEvent.toolName = toolName;
                toolEvent.toolUseId = toolUseId;
                toolEvent.toolInput = toolInput;
                callback(toolEvent);

                StreamEvent stop;
                stop.type = StreamEvent::EVT_STOP;
                stop.stopReason = "tool_use";
                callback(stop);
            } else {
                StreamEvent stop;
                stop.type = StreamEvent::EVT_STOP;
                stop.stopReason = "end_turn";
                callback(stop);
            }
        }
    );
}

int LocalLLMClient::countTokens(const std::string& text) const {
    if (engine_) return engine_->countTokens(text);
    return static_cast<int>(text.size() / 4);
}

std::string LocalLLMClient::getModelName() const {
    return "local-llm";
}

bool LocalLLMClient::supportsToolUse() const {
    // Local models support tool use via SKILL: format parsing
    return true;
}

} // namespace closecrab
