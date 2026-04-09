#pragma once

#include "../core/Message.h"
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {

class PromptSuggestionService {
public:
    static PromptSuggestionService& getInstance() {
        static PromptSuggestionService instance;
        return instance;
    }

    std::vector<std::string> suggest(const std::vector<Message>& messages,
                                     int maxSuggestions = 3) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> suggestions;

        if (messages.empty()) {
            suggestions.push_back("What would you like to work on?");
            return suggestions;
        }

        // Find last assistant and last user message
        std::string lastAssistantText;
        std::string lastUserText;
        bool hasToolCalls = false;
        bool hasErrors = false;

        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->role == MessageRole::ASSISTANT && lastAssistantText.empty()) {
                lastAssistantText = it->getText();
            }
            if (it->role == MessageRole::USER && lastUserText.empty()) {
                lastUserText = it->getText();
            }
            for (const auto& block : it->content) {
                if (block.type == ContentBlockType::TOOL_USE) hasToolCalls = true;
                if (block.type == ContentBlockType::TOOL_RESULT && block.isError) hasErrors = true;
            }
            if (!lastAssistantText.empty() && !lastUserText.empty()) break;
        }

        // Rule-based suggestion engine
        std::string lowerAssistant = toLower(lastAssistantText);
        std::string lowerUser = toLower(lastUserText);

        // If last assistant message mentioned files/changes
        if (containsAny(lowerAssistant, {"wrote to", "created file", "edited", "modified",
                                          "updated file", "saved"})) {
            suggestions.push_back("Review the changes");
            suggestions.push_back("Run tests");
        }

        // If there was an error
        if (hasErrors || containsAny(lowerAssistant, {"error", "failed", "exception", "traceback"})) {
            suggestions.push_back("Fix the error");
            suggestions.push_back("Show me the error details");
        }

        // If conversation is about code
        if (containsAny(lowerAssistant, {"function", "class", "method", "implementation",
                                          "code", "def ", "void ", "int "})) {
            if (!containsAny(lowerUser, {"explain", "what does"})) {
                suggestions.push_back("Explain this code");
            }
            suggestions.push_back("Add tests for this");
        }

        // If tool calls were made
        if (hasToolCalls) {
            suggestions.push_back("What did you change?");
        }

        // Generic fallbacks
        if (suggestions.empty()) {
            suggestions.push_back("Continue");
            suggestions.push_back("Summarize what we've done");
        }

        // Deduplicate and limit
        std::vector<std::string> unique;
        for (const auto& s : suggestions) {
            if (std::find(unique.begin(), unique.end(), s) == unique.end()) {
                unique.push_back(s);
            }
            if (static_cast<int>(unique.size()) >= maxSuggestions) break;
        }

        spdlog::debug("PromptSuggestion: generated {} suggestions", unique.size());
        return unique;
    }

private:
    PromptSuggestionService() = default;
    mutable std::mutex mutex_;

    static std::string toLower(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    static bool containsAny(const std::string& text, const std::vector<std::string>& keywords) {
        for (const auto& kw : keywords) {
            if (text.find(kw) != std::string::npos) return true;
        }
        return false;
    }
};

} // namespace closecrab
