#pragma once

#include "CompactStrategy.h"
#include "AutoCompactStrategy.h"
#include "MicroCompactStrategy.h"
#include "ReactiveCompactStrategy.h"
#include "ContextCollapseStrategy.h"
#include "EnhancedSnipStrategy.h"
#include "TokenEstimator.h"
#include "Message.h"
#include "../api/APIClient.h"
#include <vector>
#include <memory>
#include <string>
#include <spdlog/spdlog.h>

namespace closecrab {

// HistoryCompactor — strategy orchestrator that delegates compaction
// to the first matching CompactStrategy in priority order.
class HistoryCompactor {
public:
    struct Config {
        int maxContextTokens = 128000;
        float compactThreshold = 0.75f;
        int keepRecentMessages = 10;
        int summaryMaxTokens = 500;
        bool enableAutoCompact = true;
        bool enableMicroCompact = true;
        bool enableReactiveCompact = true;
        bool enableContextCollapse = true;
        bool enableSnipCompact = true;
    };

    explicit HistoryCompactor(const Config& config = {})
        : config_(config)
    {
        registerStrategies();
    }

    // Estimate tokens and run the first strategy whose shouldTrigger fires.
    // Returns true if any compaction was performed.
    bool compactIfNeeded(std::vector<Message>& messages, APIClient* apiClient) {
        if (messages.empty()) return false;

        int estimatedTokens = TokenEstimator::estimateMessages(messages);

        for (auto& strategy : strategies_) {
            if (strategy->shouldTrigger(messages, estimatedTokens, config_.maxContextTokens)) {
                spdlog::info("HistoryCompactor: strategy '{}' triggered ({} tokens, max {})",
                             strategy->name(), estimatedTokens, config_.maxContextTokens);
                lastMetadata_ = strategy->compact(messages, apiClient, config_.maxContextTokens);
                return true;
            }
        }
        return false;
    }

    // Force compaction using AutoCompactStrategy directly.
    bool forceCompact(std::vector<Message>& messages, APIClient* apiClient) {
        if (messages.empty()) return false;

        spdlog::info("HistoryCompactor: forceCompact via AutoCompactStrategy");
        AutoCompactStrategy autoStrategy;
        lastMetadata_ = autoStrategy.compact(messages, apiClient, config_.maxContextTokens);
        return lastMetadata_.messagesSummarized > 0;
    }

    const CompactMetadata& getLastMetadata() const { return lastMetadata_; }

    void setMaxContextTokens(int tokens) { config_.maxContextTokens = tokens; }

    const Config& getConfig() const { return config_; }

private:
    void registerStrategies() {
        // Priority order: reactive (emergency) first, then auto, micro, collapse, snip
        if (config_.enableReactiveCompact)
            strategies_.push_back(std::make_unique<ReactiveCompactStrategy>());
        if (config_.enableAutoCompact)
            strategies_.push_back(std::make_unique<AutoCompactStrategy>());
        if (config_.enableMicroCompact)
            strategies_.push_back(std::make_unique<MicroCompactStrategy>());
        if (config_.enableContextCollapse)
            strategies_.push_back(std::make_unique<ContextCollapseStrategy>());
        if (config_.enableSnipCompact)
            strategies_.push_back(std::make_unique<EnhancedSnipStrategy>());
    }

    Config config_;
    std::vector<std::unique_ptr<CompactStrategy>> strategies_;
    CompactMetadata lastMetadata_;
};

} // namespace closecrab
