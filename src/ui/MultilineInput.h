#pragma once
#include <string>
#include <iostream>

namespace closecrab {

class MultilineInput {
public:
    // Check if input needs continuation (ends with \ or has unclosed code block)
    static bool needsContinuation(const std::string& input) {
        if (input.empty()) return false;
        // Backslash continuation
        if (input.back() == '\\') return true;
        // Unclosed triple backtick
        int backtickCount = 0;
        size_t pos = 0;
        while ((pos = input.find("```", pos)) != std::string::npos) {
            backtickCount++;
            pos += 3;
        }
        return (backtickCount % 2) != 0;
    }

    // Read multiline input until complete
    static std::string readComplete(const std::string& firstLine,
                                     const std::string& continuationPrompt = ". ") {
        std::string result = firstLine;

        while (needsContinuation(result)) {
            // Remove trailing backslash if present
            if (!result.empty() && result.back() == '\\') {
                result.pop_back();
                result += '\n';
            }

            std::cout << continuationPrompt << std::flush;
            std::string line;
            if (!std::getline(std::cin, line)) break;
            result += line;
        }

        return result;
    }
};

} // namespace closecrab
