#pragma once
#include <string>

namespace closecrab {

class CompactWarning {
public:
    static std::string check(int estimatedTokens, int maxTokens = 200000) {
        float usage = (float)estimatedTokens / maxTokens;
        if (usage > 0.9f) {
            return "\033[31m[!] Context 90% full (" + std::to_string(estimatedTokens/1000) +
                   "k/" + std::to_string(maxTokens/1000) + "k tokens). Use /compact to free space.\033[0m\n";
        } else if (usage > 0.8f) {
            return "\033[33m[i] Context 80% full (" + std::to_string(estimatedTokens/1000) +
                   "k/" + std::to_string(maxTokens/1000) + "k tokens). Consider /compact soon.\033[0m\n";
        }
        return "";
    }
};

} // namespace closecrab
