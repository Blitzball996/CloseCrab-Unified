#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <spdlog/spdlog.h>
#include "Message.h"
#include "HistoryCompactor.h"
#include "MicroCompactStrategy.h"
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

    // Handle prompt-too-long error: surgical compaction (NOT a nuke).
    //
    // The OLD behavior summarized the whole conversation down to a handful of
    // messages (302 → 11 in one real session — kkk.txt), which is exactly why
    // the user "卡 503 一会会就忘了之前在做什么": 96% of context vanished, so
    // after typing "继续" the model had no idea what it was doing.
    //
    // JackProAi's answer is microcompact (services/compact/microCompact.ts):
    // clear only OLD tool_result CONTENT (replace with a placeholder) while
    // keeping every message and all the actual dialogue, plus the most recent
    // results intact. We do the same via MicroCompactStrategy, which truncates
    // oversized tool_result/text blocks in the older messages and leaves the
    // last ~10 untouched — the conversation structure (and the task) survives.
    static RecoveryResult handlePromptTooLong(
        std::vector<Message>& messages,
        APIClient* apiClient,
        HistoryCompactor& compactor)
    {
        spdlog::info("Handling prompt-too-long / post-503: surgical micro-compaction "
                     "(clear old tool results, keep the conversation)");

        size_t beforeSize = messages.size();
        MicroCompactStrategy micro;
        // maxContextTokens isn't used by MicroCompactStrategy::compact (it trims
        // by block size, not a global budget), so any value is fine here.
        auto meta = micro.compact(messages, apiClient, compactor.getConfig().maxContextTokens);

        if (meta.postTokens < meta.preTokens) {
            spdlog::info("Micro-compaction freed context: {} -> {} tokens, {} messages preserved",
                         meta.preTokens, meta.postTokens, messages.size());
            return {true, "compacted",
                    "Micro-compaction (cleared old tool results, conversation preserved)"};
        }

        // Nothing left to trim in the old tool results. As a last resort drop
        // old SYSTEM messages (still non-destructive to the dialogue), and only
        // if THAT fails do we fall back to the heavy summarizer.
        spdlog::info("Micro-compaction found nothing to trim; removing old SYSTEM messages");
        auto sysResult = removeOldSystemMessages(messages);
        if (sysResult.success) return sysResult;

        // Genuinely irrecoverable by surgical means — only now summarize, and
        // keep more recent turns than the old aggressive path (keepRecent 4 was
        // far too few; that's what produced the 11-message wipe).
        spdlog::warn("Surgical compaction insufficient; falling back to summary (keepRecent=12)");
        HistoryCompactor::Config cfg;
        cfg.maxContextTokens = compactor.getConfig().maxContextTokens;
        cfg.compactThreshold = 0.0f;
        cfg.keepRecentMessages = 12;
        cfg.summaryMaxTokens = 600;
        HistoryCompactor summarizer(cfg);
        bool compacted = summarizer.forceCompact(messages, apiClient);
        if (compacted && messages.size() < beforeSize) {
            return {true, "compacted", "Summary fallback (kept last 12 turns)"};
        }
        return {false, "abort", "Cannot compact further"};
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

        // English / Anthropic-native signals
        if (lower.find("prompt is too long") != std::string::npos) return true;
        if (lower.find("prompt too long") != std::string::npos) return true;
        if (lower.find("context_length_exceeded") != std::string::npos) return true;
        if (lower.find("context length") != std::string::npos &&
            lower.find("exceed") != std::string::npos) return true;
        if (lower.find("too many tokens") != std::string::npos) return true;
        if (lower.find("maximum context") != std::string::npos) return true;
        if (lower.find("input is too long") != std::string::npos) return true;
        if (lower.find("max_tokens") != std::string::npos &&
            lower.find("exceed") != std::string::npos) return true;
        if (lower.find("request entity too large") != std::string::npos) return true;
        if (lower.find("payload too large") != std::string::npos) return true;
        // Proxy-localized signals (yikoulian.cc and similar relays return the
        // size-limit error in Chinese; match on the original, non-lowered text
        // since CJK has no case). Covers "上下文过长 / 超出 / 太长 / 令牌超限".
        if (msg.find("\xe4\xb8\x8a\xe4\xb8\x8b\xe6\x96\x87") != std::string::npos && // 上下文
            (msg.find("\xe8\xb6\x85") != std::string::npos ||   // 超(出)
             msg.find("\xe8\xbf\x87\xe9\x95\xbf") != std::string::npos || // 过长
             msg.find("\xe5\xa4\xaa\xe9\x95\xbf") != std::string::npos)) return true; // 太长
        if (msg.find("\xe4\xbb\xa4\xe7\x89\x8c") != std::string::npos && // 令牌
            msg.find("\xe8\xb6\x85") != std::string::npos) return true;   // 超(限)

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
