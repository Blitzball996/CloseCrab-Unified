#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <spdlog/spdlog.h>
#include "Message.h"
#include "HistoryCompactor.h"
#include "AppState.h"
#include "../api/APIClient.h"
#include "../api/APIError.h"

namespace closecrab {

struct RecoveryResult {
    bool success = false;
    std::string action;   // "compacted", "fallback", "abort"
    std::string reason;
};

class ErrorRecovery {
public:
    static constexpr int MAX_RECOVERY_ATTEMPTS = 3;

    // Handle max_tokens stop reason: force compact and retry
    static RecoveryResult handleMaxOutputTokens(
        std::vector<Message>& messages,
        APIClient* apiClient,
        HistoryCompactor& compactor,
        int attempt)
    {
        if (attempt > MAX_RECOVERY_ATTEMPTS) {
            spdlog::warn("Max recovery attempts ({}) exceeded for max_tokens", MAX_RECOVERY_ATTEMPTS);
            return {false, "abort", "Max recovery attempts exceeded"};
        }

        spdlog::info("Handling max_tokens (attempt {}/{}), forcing compaction", attempt, MAX_RECOVERY_ATTEMPTS);
        bool compacted = compactor.forceCompact(messages, apiClient);

        if (compacted) {
            return {true, "compacted",
                    "Compacted history after max_tokens, attempt " + std::to_string(attempt)};
        }

        return {false, "abort", "Cannot compact further"};
    }

    // Handle prompt-too-long error: aggressive compaction
    static RecoveryResult handlePromptTooLong(
        std::vector<Message>& messages,
        APIClient* apiClient,
        HistoryCompactor& compactor)
    {
        spdlog::info("Handling prompt-too-long error, attempting aggressive compaction");

        // First try: aggressive compaction with a reactive strategy
        // Use a temporary compactor with very aggressive settings
        HistoryCompactor::Config aggressiveConfig;
        aggressiveConfig.maxContextTokens = compactor.getConfig().maxContextTokens;  // keep same window
        aggressiveConfig.compactThreshold = 0.0f;  // Always compact (threshold = 0)
        aggressiveConfig.keepRecentMessages = 4;    // Keep only last 4 messages
        aggressiveConfig.summaryMaxTokens = 300;    // Shorter summary

        HistoryCompactor reactiveCompactor(aggressiveConfig);
        size_t beforeSize = messages.size();
        bool compacted = reactiveCompactor.forceCompact(messages, apiClient);

        if (compacted && messages.size() < beforeSize) {
            spdlog::info("Aggressive compaction reduced messages from {} to {}", beforeSize, messages.size());
            return {true, "compacted", "Aggressive compaction for prompt too long"};
        }

        // Second try: remove all SYSTEM messages except the last compact boundary
        spdlog::info("Aggressive compaction insufficient, removing old SYSTEM messages");
        return removeOldSystemMessages(messages);
    }

    // Handle generic errors by falling back to a different model
    static RecoveryResult handleFallback(AppState* appState, const std::string& originalError) {
        if (!appState) {
            return {false, "abort", "No app state available"};
        }

        if (!appState->fallbackModel.empty() &&
            appState->fallbackModel != appState->currentModel)
        {
            spdlog::warn("Falling back from model '{}' to '{}' due to error: {}",
                         appState->currentModel, appState->fallbackModel, originalError);
            return {true, "fallback",
                    "Switching to fallback model: " + appState->fallbackModel};
        }

        return {false, "abort", "No fallback model available"};
    }

    // Check if an API error indicates the prompt is too long
    static bool isPromptTooLong(const APIError& e) {
        std::string msg = e.what();
        // Convert to lowercase for case-insensitive matching
        std::string lower;
        lower.resize(msg.size());
        std::transform(msg.begin(), msg.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (lower.find("prompt is too long") != std::string::npos) return true;
        if (lower.find("context_length_exceeded") != std::string::npos) return true;
        if (e.httpStatus == 413) return true;

        return false;
    }

    // Check if the stop reason indicates max output tokens
    static bool isMaxOutputTokens(const std::string& stopReason) {
        return stopReason == "max_tokens";
    }

private:
    // Remove old SYSTEM messages, keeping only the last compact boundary
    static RecoveryResult removeOldSystemMessages(std::vector<Message>& messages) {
        // Find the last compact boundary
        int lastBoundaryIdx = -1;
        for (int i = static_cast<int>(messages.size()) - 1; i >= 0; i--) {
            if (messages[i].type == MessageType::SYSTEM &&
                messages[i].systemSubtype == SystemSubtype::COMPACT_BOUNDARY) {
                lastBoundaryIdx = i;
                break;
            }
        }

        // Remove all SYSTEM messages except the last compact boundary
        size_t beforeSize = messages.size();
        std::vector<Message> filtered;
        filtered.reserve(messages.size());

        for (int i = 0; i < static_cast<int>(messages.size()); i++) {
            if (messages[i].type == MessageType::SYSTEM) {
                // Keep only the last compact boundary
                if (i == lastBoundaryIdx) {
                    filtered.push_back(std::move(messages[i]));
                }
                // Skip all other SYSTEM messages
            } else {
                filtered.push_back(std::move(messages[i]));
            }
        }

        if (filtered.size() < beforeSize) {
            messages = std::move(filtered);
            spdlog::info("Removed {} SYSTEM messages, kept last compact boundary",
                         beforeSize - messages.size());
            return {true, "compacted",
                    "Removed old SYSTEM messages (" + std::to_string(beforeSize - messages.size()) + " removed)"};
        }

        return {false, "abort", "No SYSTEM messages to remove"};
    }
};

} // namespace closecrab
