#pragma once

#include <string>
#include <atomic>
#include <spdlog/spdlog.h>

namespace closecrab {

// Policy limits engine — enforces token budgets and cost caps
// Mirrors JackProAi's policyLimits service
class PolicyLimits {
public:
    static PolicyLimits& getInstance() {
        static PolicyLimits instance;
        return instance;
    }

    struct Limits {
        int maxTurnsPerSession = 0;      // 0 = unlimited
        double maxCostPerSession = 0.0;  // 0 = unlimited (USD)
        int maxTokensPerTurn = 0;        // 0 = unlimited
        int maxOutputTokens = 8192;      // Max output tokens per API call
        int maxContextTokens = 128000;   // Max context window
        bool allowDestructiveTools = true;
    };

    void setLimits(const Limits& limits) { limits_ = limits; }
    const Limits& getLimits() const { return limits_; }

    // Check if an action is within limits
    bool checkTurnLimit(int currentTurn) const {
        if (limits_.maxTurnsPerSession <= 0) return true;
        if (currentTurn > limits_.maxTurnsPerSession) {
            spdlog::warn("Turn limit reached: {}/{}", currentTurn, limits_.maxTurnsPerSession);
            return false;
        }
        return true;
    }

    bool checkCostLimit(double currentCost) const {
        if (limits_.maxCostPerSession <= 0.0) return true;
        if (currentCost > limits_.maxCostPerSession) {
            spdlog::warn("Cost limit reached: ${:.4f}/${:.4f}", currentCost, limits_.maxCostPerSession);
            return false;
        }
        return true;
    }

    bool checkDestructiveTool() const {
        return limits_.allowDestructiveTools;
    }

    std::string getSummary() const {
        std::string s;
        s += "Max turns/session: " + (limits_.maxTurnsPerSession > 0 ?
            std::to_string(limits_.maxTurnsPerSession) : "unlimited") + "\n";
        s += "Max cost/session: " + (limits_.maxCostPerSession > 0 ?
            "$" + std::to_string(limits_.maxCostPerSession) : "unlimited") + "\n";
        s += "Max output tokens: " + std::to_string(limits_.maxOutputTokens) + "\n";
        s += "Max context tokens: " + std::to_string(limits_.maxContextTokens) + "\n";
        s += "Destructive tools: " + std::string(limits_.allowDestructiveTools ? "allowed" : "blocked") + "\n";
        return s;
    }

private:
    PolicyLimits() = default;
    Limits limits_;
};

} // namespace closecrab
