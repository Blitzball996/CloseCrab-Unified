#pragma once
#include <string>
#include <vector>
#include <set>
#include <random>
#include <chrono>

namespace closecrab {

class Tips {
public:
    static Tips& getInstance() {
        static Tips instance;
        return instance;
    }

    // Get a contextual tip based on what just happened
    std::string getTip(const std::string& context = "") const {
        if (!context.empty()) {
            // Context-specific tips
            if (context == "permission_denied") {
                return "Tip: Use /permissions bypass to skip all permission prompts";
            }
            if (context == "long_output") {
                return "Tip: Use /compact to compress conversation history when it gets long";
            }
            if (context == "first_session") {
                return "Tip: Type /help to see all commands, or just describe what you want to build";
            }
            if (context == "error_loop") {
                return "Tip: Try /rewind to undo the last turn, or describe the problem differently";
            }
            if (context == "many_edits") {
                return "Tip: Use /diff to review changes, then /commit to save your work";
            }
        }

        // Random general tip
        return getRandomTip();
    }

    // Show tip only once per session for each tip ID
    bool shouldShow(const std::string& tipId) {
        if (shownTips_.count(tipId)) return false;
        shownTips_.insert(tipId);
        return true;
    }

    static std::string format(const std::string& tip) {
        if (tip.empty()) return "";
        return "\033[90m  " + tip + "\033[0m\n";
    }

private:
    Tips() = default;

    std::string getRandomTip() const {
        static const std::vector<std::string> tips = {
            "Tip: Press 'a' at permission prompts to approve all for this session",
            "Tip: Use /model to switch between different AI models",
            "Tip: Use /resume to continue a previous conversation",
            "Tip: Use /search <keyword> to find past conversations",
            "Tip: Use /thinking on to enable extended reasoning mode",
            "Tip: Use /fast for shorter, quicker responses",
            "Tip: Prefix commands with ! to run them directly (e.g. !git status)",
            "Tip: Use /coordinator for complex multi-step tasks",
            "Tip: Use /export to save your conversation as markdown",
            "Tip: Use /cost to see your API token usage"
        };

        auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        std::mt19937 rng(static_cast<unsigned>(seed));
        std::uniform_int_distribution<int> dist(0, (int)tips.size() - 1);
        return tips[dist(rng)];
    }

    std::set<std::string> shownTips_;
};

} // namespace closecrab
