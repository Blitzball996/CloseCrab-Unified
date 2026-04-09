#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <spdlog/spdlog.h>
#include "TokenEstimator.h"

namespace closecrab {

struct TokenBudget {
    int queryBudget = 0;       // Max tokens for this query (0 = unlimited)
    int taskBudget = 0;        // Max tokens for the entire task/session
    int toolResultBudget = 16000; // Max tokens per individual tool result
};

class BudgetTracker {
public:
    explicit BudgetTracker(const TokenBudget& budget = {}) : budget_(budget) {}

    // Check remaining query budget. Returns remaining tokens, or -1 if unlimited.
    int checkQueryBudget(int currentTokens) const {
        if (budget_.queryBudget <= 0) return -1;
        return budget_.queryBudget - queryConsumed_.load() - currentTokens;
    }

    int checkTaskBudget(int currentTokens) const {
        if (budget_.taskBudget <= 0) return -1;
        return budget_.taskBudget - taskConsumed_.load() - currentTokens;
    }

    // Apply tool result budget: truncate content if over limit
    std::string applyToolResultBudget(const std::string& content) const {
        if (budget_.toolResultBudget <= 0) return content;
        int estimated = TokenEstimator::estimate(content);
        if (estimated <= budget_.toolResultBudget) return content;

        // Truncate to fit budget (approximate char count from token budget)
        size_t maxChars = static_cast<size_t>(budget_.toolResultBudget) * 4;
        if (maxChars >= content.size()) return content;

        // Find a clean break point (newline or space)
        size_t breakAt = maxChars;
        for (size_t i = maxChars; i > maxChars - 200 && i > 0; i--) {
            if (content[i] == '\n') { breakAt = i; break; }
        }

        std::string truncated = content.substr(0, breakAt);
        truncated += "\n\n...[truncated: original was " + std::to_string(content.size())
                   + " chars, ~" + std::to_string(estimated) + " tokens, budget is "
                   + std::to_string(budget_.toolResultBudget) + " tokens]";
        spdlog::debug("Tool result truncated: {} -> {} tokens", estimated, TokenEstimator::estimate(truncated));
        return truncated;
    }

    // Track consumption
    void consumeQueryTokens(int tokens) { queryConsumed_ += tokens; }
    void consumeTaskTokens(int tokens) { taskConsumed_ += tokens; }

    bool isQueryBudgetExceeded() const {
        return budget_.queryBudget > 0 && queryConsumed_.load() >= budget_.queryBudget;
    }
    bool isTaskBudgetExceeded() const {
        return budget_.taskBudget > 0 && taskConsumed_.load() >= budget_.taskBudget;
    }

    int getRemainingQueryTokens() const {
        if (budget_.queryBudget <= 0) return -1;
        return std::max(0, budget_.queryBudget - queryConsumed_.load());
    }
    int getRemainingTaskTokens() const {
        if (budget_.taskBudget <= 0) return -1;
        return std::max(0, budget_.taskBudget - taskConsumed_.load());
    }

    void resetQueryBudget() { queryConsumed_ = 0; }

    const TokenBudget& getBudget() const { return budget_; }
    void setBudget(const TokenBudget& b) { budget_ = b; }

private:
    TokenBudget budget_;
    std::atomic<int> queryConsumed_{0};
    std::atomic<int> taskConsumed_{0};
};

} // namespace closecrab
