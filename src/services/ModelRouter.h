#pragma once
#include <string>
#include <vector>
#include <regex>

namespace closecrab {

enum class TaskComplexity { SIMPLE, MODERATE, COMPLEX };

class ModelRouter {
public:
    struct ModelChoice {
        std::string model;
        TaskComplexity complexity;
        std::string reason;
    };

    // Analyze a user prompt and recommend which model to use
    static ModelChoice route(const std::string& prompt, const std::string& defaultModel = "") {
        TaskComplexity complexity = analyzeComplexity(prompt);
        ModelChoice choice;
        choice.complexity = complexity;

        switch (complexity) {
            case TaskComplexity::SIMPLE:
                choice.model = "claude-haiku-4-5-20251001";
                choice.reason = "Simple task (short prompt, single action)";
                break;
            case TaskComplexity::MODERATE:
                choice.model = "claude-sonnet-4-20250514";
                choice.reason = "Moderate complexity (multi-step, code generation)";
                break;
            case TaskComplexity::COMPLEX:
                choice.model = "claude-opus-4-20250514";
                choice.reason = "Complex task (architecture, multi-file, debugging)";
                break;
        }

        // If user has a default model set, only downgrade, never upgrade
        if (!defaultModel.empty() && defaultModel.find("opus") != std::string::npos) {
            choice.model = defaultModel;
            choice.reason = "Using configured model (opus)";
        }

        return choice;
    }

private:
    static TaskComplexity analyzeComplexity(const std::string& prompt) {
        // Length heuristic
        if (prompt.size() < 50) return TaskComplexity::SIMPLE;
        if (prompt.size() > 500) return TaskComplexity::COMPLEX;

        // Keyword-based complexity detection
        static const std::vector<std::string> complexKeywords = {
            "architect", "design", "refactor", "security", "performance",
            "optimize", "debug", "investigate", "analyze", "multi-file",
            "system", "infrastructure", "migration", "rewrite"
        };
        static const std::vector<std::string> simpleKeywords = {
            "fix typo", "rename", "add comment", "format", "lint",
            "what is", "show me", "list", "help", "version"
        };

        std::string lower = prompt;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        for (const auto& kw : simpleKeywords) {
            if (lower.find(kw) != std::string::npos) return TaskComplexity::SIMPLE;
        }
        for (const auto& kw : complexKeywords) {
            if (lower.find(kw) != std::string::npos) return TaskComplexity::COMPLEX;
        }

        // Count action indicators
        int actionCount = 0;
        for (const auto& indicator : {"and ", "then ", "also ", "after that", "finally"}) {
            if (lower.find(indicator) != std::string::npos) actionCount++;
        }
        if (actionCount >= 3) return TaskComplexity::COMPLEX;
        if (actionCount >= 1) return TaskComplexity::MODERATE;

        return TaskComplexity::MODERATE;
    }
};

} // namespace closecrab
