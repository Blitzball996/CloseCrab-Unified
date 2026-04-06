#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace closecrab {

struct ModelCost {
    double inputPricePerMToken = 3.0;   // $/M tokens
    double outputPricePerMToken = 15.0;
};

class CostTracker {
public:
    static CostTracker& getInstance() {
        static CostTracker instance;
        return instance;
    }

    void setPricing(const std::string& model, double inputPrice, double outputPrice) {
        std::lock_guard<std::mutex> lock(mutex_);
        pricing_[model] = {inputPrice, outputPrice};
    }

    void track(const std::string& model, int64_t inputTokens, int64_t outputTokens,
               int64_t cacheRead = 0, int64_t cacheWrite = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& u = usage_[model];
        u.inputTokens += inputTokens;
        u.outputTokens += outputTokens;
        u.cacheReadTokens += cacheRead;
        u.cacheWriteTokens += cacheWrite;

        // Calculate cost
        auto pit = pricing_.find(model);
        ModelCost price = (pit != pricing_.end()) ? pit->second : ModelCost{0, 0};
        double cost = (inputTokens * price.inputPricePerMToken +
                       outputTokens * price.outputPricePerMToken) / 1000000.0;
        u.costUSD += cost;
        totalCost_.store(totalCost_.load() + cost);
    }

    double getTotalCost() const { return totalCost_.load(); }

    std::string getSummary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        for (const auto& [model, u] : usage_) {
            oss << model << ": " << u.inputTokens << " in / " << u.outputTokens
                << " out ($" << u.costUSD << ")\n";
        }
        oss << "Total: $" << totalCost_.load() << "\n";
        return oss.str();
    }

    void loadPricingDefaults() {
        setPricing("claude-opus-4-6", 15.0, 75.0);
        setPricing("claude-sonnet-4-6", 3.0, 15.0);
        setPricing("claude-haiku-4-5", 0.8, 4.0);
        setPricing("local-llm", 0.0, 0.0);
    }

private:
    CostTracker() { loadPricingDefaults(); }

    struct Usage {
        int64_t inputTokens = 0;
        int64_t outputTokens = 0;
        int64_t cacheReadTokens = 0;
        int64_t cacheWriteTokens = 0;
        double costUSD = 0.0;
    };

    mutable std::mutex mutex_;
    std::map<std::string, Usage> usage_;
    std::map<std::string, ModelCost> pricing_;
    std::atomic<double> totalCost_{0.0};
};

} // namespace closecrab
