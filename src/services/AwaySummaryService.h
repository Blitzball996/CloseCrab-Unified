#pragma once

#include "../core/Message.h"
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {

class AwaySummaryService {
public:
    static AwaySummaryService& getInstance() {
        static AwaySummaryService instance;
        return instance;
    }

    void markAway() {
        std::lock_guard<std::mutex> lock(mutex_);
        awayTimestamp_ = std::chrono::system_clock::now();
        isAway_ = true;
        spdlog::info("AwaySummary: user marked as away");
    }

    bool isAway() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return isAway_;
    }

    std::chrono::system_clock::time_point getAwayTimestamp() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return awayTimestamp_;
    }

    std::string generateSummary(const std::vector<Message>& messages,
                                std::chrono::system_clock::time_point awayTimestamp) {
        std::lock_guard<std::mutex> lock(mutex_);
        isAway_ = false;

        int messageCount = 0;
        int toolCalls = 0;
        int errors = 0;
        std::vector<std::string> toolNames;

        auto awayEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(
            awayTimestamp.time_since_epoch()).count();

        for (const auto& msg : messages) {
            // Only count messages after the away timestamp
            if (msg.timestamp < awayEpoch) continue;

            ++messageCount;

            for (const auto& block : msg.content) {
                if (block.type == ContentBlockType::TOOL_USE) {
                    ++toolCalls;
                    if (std::find(toolNames.begin(), toolNames.end(), block.toolName) == toolNames.end()) {
                        toolNames.push_back(block.toolName);
                    }
                }
                if (block.type == ContentBlockType::TOOL_RESULT && block.isError) {
                    ++errors;
                }
            }
        }

        // Calculate away duration
        auto now = std::chrono::system_clock::now();
        auto awayDuration = std::chrono::duration_cast<std::chrono::minutes>(now - awayTimestamp);

        std::string summary = "Welcome back! You were away for " +
            std::to_string(awayDuration.count()) + " minutes.\n\n";

        if (messageCount == 0) {
            summary += "Nothing happened while you were away.";
            spdlog::info("AwaySummary: user returned, no activity during absence");
            return summary;
        }

        summary += "While you were away:\n";
        summary += "- " + std::to_string(messageCount) + " messages exchanged\n";

        if (toolCalls > 0) {
            summary += "- " + std::to_string(toolCalls) + " tool calls made";
            if (!toolNames.empty()) {
                summary += " (";
                for (size_t i = 0; i < toolNames.size(); ++i) {
                    if (i > 0) summary += ", ";
                    summary += toolNames[i];
                }
                summary += ")";
            }
            summary += "\n";
        }

        if (errors > 0) {
            summary += "- " + std::to_string(errors) + " error(s) encountered\n";
        }

        spdlog::info("AwaySummary: user returned after {}min, {} msgs, {} tools, {} errors",
                     awayDuration.count(), messageCount, toolCalls, errors);
        return summary;
    }

private:
    AwaySummaryService() = default;
    mutable std::mutex mutex_;
    bool isAway_ = false;
    std::chrono::system_clock::time_point awayTimestamp_;
};

} // namespace closecrab
