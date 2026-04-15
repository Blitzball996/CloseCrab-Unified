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
                // Format tool input as key-value text instead of JSON dump()
                // dump() escapes backslashes (G:\path -> G:\\path) which confuses the LLM
                text += "PARAMS:\n";
                if (block.toolInput.is_object()) {
                    for (auto& [key, val] : block.toolInput.items()) {
                        text += "  " + key + ": ";
                        if (val.is_string()) {
                            text += val.get<std::string>();
                        } else {
                            text += val.dump();
                        }
                        text += "\n";
                    }
                } else {
                    text += "  " + block.toolInput.dump() + "\n";
                }
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
    // Strategy 1: Try JSON tool_use format (common in fine-tuned models)
    // Look for {"tool_use": {"name": "...", "input": {...}}}
    try {
        // Find JSON object in text
        auto start = text.find('{');
        auto end = text.rfind('}');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string jsonStr = text.substr(start, end - start + 1);
            auto j = nlohmann::json::parse(jsonStr);

            if (j.contains("tool_use") && j["tool_use"].contains("name")) {
                toolName = j["tool_use"]["name"].get<std::string>();
                toolUseId = generateUUID();
                toolInput = j["tool_use"].value("input", nlohmann::json::object());
                return true;
            }
            // Alternative format: {"name": "...", "input": {...}}
            if (j.contains("name") && j.contains("input")) {
                toolName = j["name"].get<std::string>();
                toolUseId = generateUUID();
                toolInput = j["input"];
                return true;
            }
            // Alternative: {"tool": "...", "arguments": {...}}
            if (j.contains("tool") && j.contains("arguments")) {
                toolName = j["tool"].get<std::string>();
                toolUseId = generateUUID();
                toolInput = j["arguments"];
                return true;
            }
        }
    } catch (...) {}

    // Strategy 2: SKILL: format (CloseCrab native)
    std::regex skillRegex(R"(SKILL:\s*(\w+)\s*\n)");
    std::smatch match;
    if (std::regex_search(text, match, skillRegex)) {
        toolName = match[1].str();
        toolUseId = generateUUID();

        // Find PARAMS: section
        std::string afterSkill = text.substr(match.position() + match.length());

        // Try to parse PARAMS as JSON first
        auto paramsPos = afterSkill.find("PARAMS:");
        if (paramsPos != std::string::npos) {
            std::string paramsStr = afterSkill.substr(paramsPos + 7);
            // Trim leading whitespace
            while (!paramsStr.empty() && (paramsStr.front() == ' ' || paramsStr.front() == '\n'))
                paramsStr.erase(paramsStr.begin());

            // Try JSON parse
            try {
                auto jsonStart = paramsStr.find('{');
                if (jsonStart != std::string::npos) {
                    auto jsonEnd = paramsStr.rfind('}');
                    if (jsonEnd != std::string::npos) {
                        toolInput = nlohmann::json::parse(paramsStr.substr(jsonStart, jsonEnd - jsonStart + 1));
                        return true;
                    }
                }
            } catch (...) {}

            // Fallback: key=value parsing
            toolInput = nlohmann::json::object();
            std::regex kvRegex(R"((\w+)\s*=\s*(.+))");
            std::istringstream iss(paramsStr);
            std::string line;
            while (std::getline(iss, line)) {
                std::smatch kvMatch;
                if (std::regex_search(line, kvMatch, kvRegex)) {
                    std::string key = kvMatch[1].str();
                    std::string val = kvMatch[2].str();
                    while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ','))
                        val.pop_back();
                    // Try to parse value as JSON (for nested objects)
                    try {
                        toolInput[key] = nlohmann::json::parse(val);
                    } catch (...) {
                        toolInput[key] = val;
                    }
                }
            }
            return true;
        }

        // No PARAMS section — tool with no arguments
        toolInput = nlohmann::json::object();
        return true;
    }

    // Strategy 3: Function call format (some models use this)
    // e.g., Read(file_path="/path/to/file")
    std::regex funcRegex(R"((\w+)\(([^)]*)\))");
    if (std::regex_search(text, match, funcRegex)) {
        std::string funcName = match[1].str();
        // Only match known tool-like names (capitalized)
        if (!funcName.empty() && std::isupper(funcName[0])) {
            toolName = funcName;
            toolUseId = generateUUID();
            toolInput = nlohmann::json::object();

            std::string argsStr = match[2].str();
            std::regex argRegex(R"xx((\w+)\s*=\s*"([^"]*)")xx");            auto abegin = std::sregex_iterator(argsStr.begin(), argsStr.end(), argRegex);
            auto aend = std::sregex_iterator();
            for (auto it = abegin; it != aend; ++it) {
                toolInput[(*it)[1].str()] = (*it)[2].str();
            }
            if (!toolInput.empty()) return true;
        }
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
