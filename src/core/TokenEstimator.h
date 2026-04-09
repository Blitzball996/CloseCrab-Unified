#pragma once
#include <string>
#include <vector>
#include "Message.h"
#include <nlohmann/json.hpp>

namespace closecrab {

class TokenEstimator {
public:
    // Fast estimation: ~4 chars per token for English/code, ~2 chars per token for CJK
    static int estimate(const std::string& text) {
        if (text.empty()) return 0;
        int cjkChars = 0;
        int totalChars = 0;
        for (size_t i = 0; i < text.size(); ) {
            unsigned char c = text[i];
            if (c >= 0xE0) { // CJK characters are 3-byte UTF-8
                cjkChars++;
                if (c >= 0xF0) i += 4;
                else if (c >= 0xE0) i += 3;
                else i += 2;
            } else {
                i++;
            }
            totalChars++;
        }
        // CJK: ~2 chars per token, ASCII: ~4 chars per token
        int asciiChars = totalChars - cjkChars;
        return (asciiChars / 4) + (cjkChars / 2) + 1; // +1 to avoid 0
    }

    // Estimate tokens for a JSON object (accounts for structural overhead)
    static int estimateJson(const nlohmann::json& j) {
        std::string s = j.dump();
        return estimate(s) + 5; // JSON structural overhead
    }

    // Estimate total tokens for a message array
    static int estimateMessages(const std::vector<Message>& messages) {
        int total = 0;
        for (const auto& msg : messages) {
            total += 4; // Message overhead (role, separators)
            for (const auto& block : msg.content) {
                switch (block.type) {
                    case ContentBlockType::TEXT:
                    case ContentBlockType::THINKING:
                        total += estimate(block.text);
                        break;
                    case ContentBlockType::TOOL_USE:
                        total += estimate(block.toolName) + estimateJson(block.toolInput) + 10;
                        break;
                    case ContentBlockType::TOOL_RESULT:
                        if (block.toolResult.is_string())
                            total += estimate(block.toolResult.get<std::string>());
                        else
                            total += estimateJson(block.toolResult);
                        break;
                    case ContentBlockType::IMAGE:
                        total += 1000; // Images are roughly 1000 tokens
                        break;
                }
            }
        }
        return total;
    }
};

} // namespace closecrab
