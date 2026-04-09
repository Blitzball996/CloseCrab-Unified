#pragma once

#include "CompactStrategy.h"
#include "Message.h"
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace closecrab {

// Emergency reactive compaction — fires at 95% context usage.
// Aggressively drops history, keeps only last 5 messages,
// uses local summary only (no LLM call to avoid adding tokens).
class ReactiveCompactStrategy : public CompactStrategy {
public:
    std::string name() const override { return "reactive"; }

    bool shouldTrigger(const std::vector<Message>& messages,
                       int estimatedTokens,
                       int maxContextTokens) const override {
        (void)messages;
        return estimatedTokens > static_cast<int>(maxContextTokens * 0.95);
    }

    CompactMetadata compact(std::vector<Message>& messages,
                            APIClient* /*apiClient*/,
                            int /*maxContextTokens*/) override {
        auto t0 = std::chrono::steady_clock::now();

        CompactMetadata meta;
        meta.trigger = "reactive";
        meta.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        meta.preTokens = estimateAllTokens(messages);

        const int keepRecent = 5; // Aggressive: only 5
        if (messages.size() <= static_cast<size_t>(keepRecent + 2)) {
            meta.postTokens = meta.preTokens;
            return meta;
        }

        size_t keepStart = messages.size() - std::min(
            messages.size(), static_cast<size_t>(keepRecent));
        keepStart = adjustSplitPoint(messages, keepStart);
        if (keepStart <= 1) {
            meta.postTokens = meta.preTokens;
            return meta;
        }

        meta.messagesSummarized = static_cast<int>(keepStart);

        // Local summary only — brief, no LLM call in emergency mode
        std::string summary = "[" + std::to_string(keepStart)
            + " messages compressed due to context limit]";

        Message compactMsg = Message::makeSystem(
            SystemSubtype::COMPACT_BOUNDARY, summary);
        compactMsg.isCompactSummary = true;

        std::vector<Message> newMessages;
        newMessages.reserve(messages.size() - keepStart + 1);
        newMessages.push_back(std::move(compactMsg));
        for (size_t i = keepStart; i < messages.size(); i++) {
            newMessages.push_back(std::move(messages[i]));
        }
        messages = std::move(newMessages);

        meta.postTokens = estimateAllTokens(messages);
        meta.preservedSegment = "last " + std::to_string(messages.size() - 1) + " messages";

        auto t1 = std::chrono::steady_clock::now();
        meta.durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        spdlog::warn("ReactiveCompact (EMERGENCY): {} msgs dropped, {} -> {} tokens ({:.1f}ms)",
                     meta.messagesSummarized, meta.preTokens, meta.postTokens, meta.durationMs);
        return meta;
    }

private:
    static size_t adjustSplitPoint(const std::vector<Message>& messages, size_t point) {
        if (point >= messages.size()) return point;
        if (point > 0 && messages[point].isToolUseResult) {
            point--;
        }
        if (point < messages.size() - 1) {
            for (const auto& block : messages[point].content) {
                if (block.type == ContentBlockType::TOOL_USE) {
                    point++;
                    break;
                }
            }
        }
        return point;
    }

    static int estimateTokenCount(const std::string& text) {
        if (text.empty()) return 0;
        int cjk = 0;
        for (unsigned char c : text) {
            if (c >= 0xE0) cjk++;
        }
        return (static_cast<int>(text.size()) - cjk) / 4 + cjk / 2;
    }

    static int estimateAllTokens(const std::vector<Message>& messages) {
        int total = 0;
        for (const auto& msg : messages) {
            total += estimateTokenCount(msg.getText());
            for (const auto& block : msg.content) {
                if (block.type == ContentBlockType::TOOL_USE) {
                    total += estimateTokenCount(block.toolInput.dump());
                } else if (block.type == ContentBlockType::TOOL_RESULT) {
                    total += estimateTokenCount(
                        block.toolResult.is_string()
                            ? block.toolResult.get<std::string>()
                            : block.toolResult.dump());
                }
            }
        }
        return total;
    }
};

} // namespace closecrab
