#pragma once
#include <string>
#include <vector>
#include <map>

namespace closecrab {

class PromptSuggestion {
public:
    static PromptSuggestion& getInstance() {
        static PromptSuggestion instance;
        return instance;
    }

    struct Suggestion {
        std::string text;
        std::string description;
        int priority = 0;
    };

    // Get suggestions based on last tool used and current state
    std::vector<Suggestion> getSuggestions(const std::string& lastTool,
                                           const std::string& lastOutput,
                                           bool hasErrors) const {
        std::vector<Suggestion> suggestions;

        if (hasErrors) {
            suggestions.push_back({"Fix the error", "Ask AI to fix the reported error", 10});
            suggestions.push_back({"/rewind", "Undo last turn and try again", 8});
        }

        if (lastTool == "Write" || lastTool == "Edit") {
            suggestions.push_back({"Run tests", "Verify the changes work", 9});
            suggestions.push_back({"/diff", "Review what changed", 7});
            suggestions.push_back({"/commit", "Commit the changes", 5});
        }

        if (lastTool == "Bash" || lastTool == "PowerShell") {
            if (lastOutput.find("error") != std::string::npos ||
                lastOutput.find("Error") != std::string::npos ||
                lastOutput.find("FAILED") != std::string::npos) {
                suggestions.push_back({"Fix the build error", "Ask AI to resolve", 10});
            }
            if (lastOutput.find("warning") != std::string::npos) {
                suggestions.push_back({"Fix warnings", "Clean up compiler warnings", 6});
            }
        }

        if (lastTool == "Grep" || lastTool == "Glob") {
            suggestions.push_back({"Read the found files", "Examine the search results", 7});
            suggestions.push_back({"Refactor matches", "Apply changes to found locations", 6});
        }

        if (lastTool.empty()) {
            suggestions.push_back({"/help", "See available commands", 3});
            suggestions.push_back({"/tools", "List available tools", 3});
            suggestions.push_back({"/doctor", "Run diagnostics", 2});
        }

        // Sort by priority
        std::sort(suggestions.begin(), suggestions.end(),
            [](const Suggestion& a, const Suggestion& b) { return a.priority > b.priority; });

        // Return top 3
        if (suggestions.size() > 3) suggestions.resize(3);
        return suggestions;
    }

    // Format suggestions for display
    static std::string format(const std::vector<Suggestion>& suggestions) {
        if (suggestions.empty()) return "";
        std::string result = "\033[90m  Suggestions: ";
        for (size_t i = 0; i < suggestions.size(); i++) {
            if (i > 0) result += " | ";
            result += suggestions[i].text;
        }
        result += "\033[0m\n";
        return result;
    }

private:
    PromptSuggestion() = default;
};

} // namespace closecrab
