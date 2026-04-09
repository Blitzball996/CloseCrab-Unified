#pragma once

#include "CompactStrategy.h"
#include "Message.h"
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <spdlog/spdlog.h>

namespace closecrab {

// Enhanced snip with relevance scoring and logical grouping.
// Groups messages into conversation turns, scores by relevance,
// and removes lowest-scoring groups to free context.
class EnhancedSnipStrategy : public CompactStrategy {
public:
    std::string name() const override { return "snip"; }

    bool shouldTrigger(const std::vector<Message>& messages,
                       int estimatedTokens,
                       int maxContextTokens) const override {
        (void)messages;
        return estimatedTokens > static_cast<int>(maxContextTokens * 0.80);
    }

    CompactMetadata compact(std::vector<Message>& messages,
                            APIClient* /*apiClient*/,
                            int maxContextTokens) override {
        auto t0 = std::chrono::steady_clock::now();

        CompactMetadata meta;
        meta.trigger = "snip";
        meta.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        meta.preTokens = estimateAllTokens(messages);

        // Group messages into logical conversation turns
        auto groups = buildGroups(messages);
        if (groups.size() <= 2) {
            meta.postTokens = meta.preTokens;
            return meta;
        }

        // Score each group by relevance
        scoreGroups(groups, messages);
        // Sort groups by score (ascending — lowest first for removal)
        std::vector<size_t> sortedIdx(groups.size());
        std::iota(sortedIdx.begin(), sortedIdx.end(), 0);
        std::sort(sortedIdx.begin(), sortedIdx.end(),
                  [&](size_t a, size_t b) {
                      return groups[a].score < groups[b].score;
                  });

        // Remove lowest-scoring groups until under target
        int targetTokens = static_cast<int>(maxContextTokens * 0.60);
        int currentTokens = meta.preTokens;
        std::vector<bool> removed(groups.size(), false);
        int removedTurns = 0;

        for (size_t idx : sortedIdx) {
            if (currentTokens <= targetTokens) break;
            // Never remove the last 2 groups (recent context)
            if (idx >= groups.size() - 2) continue;

            removed[idx] = true;
            currentTokens -= groups[idx].estimatedTokens;
            removedTurns++;
        }

        if (removedTurns == 0) {
            meta.postTokens = meta.preTokens;
            return meta;
        }

        // Rebuild message list, skipping removed groups
        std::vector<Message> newMessages;
        newMessages.reserve(messages.size());

        // Insert boundary message at the front
        Message boundary = Message::makeSystem(
            SystemSubtype::COMPACT_BOUNDARY,
            "[Snip: removed " + std::to_string(removedTurns)
                + " conversation turns to free context]");
        boundary.isCompactSummary = true;
        newMessages.push_back(std::move(boundary));

        for (size_t gi = 0; gi < groups.size(); gi++) {
            if (removed[gi]) continue;
            for (size_t mi = groups[gi].startIdx; mi <= groups[gi].endIdx; mi++) {
                newMessages.push_back(std::move(messages[mi]));
            }
        }

        messages = std::move(newMessages);
        meta.messagesSummarized = removedTurns;
        meta.postTokens = estimateAllTokens(messages);
        meta.preservedSegment = "high-relevance turns + last 2 groups";

        auto t1 = std::chrono::steady_clock::now();
        meta.durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        spdlog::info("EnhancedSnip: removed {} turns, {} -> {} tokens ({:.1f}ms)",
                     removedTurns, meta.preTokens, meta.postTokens, meta.durationMs);
        return meta;
    }

private:
    struct MessageGroup {
        size_t startIdx = 0;
        size_t endIdx = 0;
        double score = 0.0;
        int estimatedTokens = 0;
        bool hasFileEdit = false;
        bool hasError = false;
    };

    // Group messages into logical turns:
    // (user_message, assistant_response, [tool_use/tool_result pairs])
    static std::vector<MessageGroup> buildGroups(const std::vector<Message>& messages) {
        std::vector<MessageGroup> groups;
        size_t i = 0;

        while (i < messages.size()) {
            MessageGroup group;
            group.startIdx = i;

            // Start with a user message (or system/other)
            if (messages[i].role == MessageRole::USER && !messages[i].isToolUseResult) {
                i++;
            } else {
                // Non-user start — include it as a single-message group
                group.endIdx = i;
                group.estimatedTokens = estimateMessageTokens(messages[i]);
                groups.push_back(group);
                i++;
                continue;
            }

            // Include assistant response and any tool_use/tool_result pairs
            while (i < messages.size()) {
                if (messages[i].role == MessageRole::ASSISTANT) {
                    // Check for file edits
                    for (const auto& block : messages[i].content) {
                        if (block.type == ContentBlockType::TOOL_USE) {
                            if (block.toolName == "Write" || block.toolName == "Edit") {
                                group.hasFileEdit = true;
                            }
                        }
                    }
                    i++;
                } else if (messages[i].isToolUseResult) {
                    // Check for errors
                    for (const auto& block : messages[i].content) {
                        if (block.type == ContentBlockType::TOOL_RESULT && block.isError) {
                            group.hasError = true;
                        }
                    }
                    i++;
                } else {
                    break; // Next user message = new group
                }
            }

            group.endIdx = i - 1;

            // Estimate tokens for the group
            group.estimatedTokens = 0;
            for (size_t j = group.startIdx; j <= group.endIdx; j++) {
                group.estimatedTokens += estimateMessageTokens(messages[j]);
            }

            groups.push_back(group);
        }
        return groups;
    }
    // Score groups by relevance: higher = more important to keep
    static void scoreGroups(std::vector<MessageGroup>& groups,
                            const std::vector<Message>& /*messages*/) {
        if (groups.empty()) return;

        for (size_t i = 0; i < groups.size(); i++) {
            double score = 0.0;

            // Recency: more recent groups score higher (linear ramp)
            score += static_cast<double>(i) / static_cast<double>(groups.size()) * 50.0;

            // File edits are high-value (Write/Edit tool usage)
            if (groups[i].hasFileEdit) score += 30.0;

            // Errors reduce relevance (likely failed attempts)
            if (groups[i].hasError) score -= 15.0;

            // Very small groups (single message) are less valuable
            size_t msgCount = groups[i].endIdx - groups[i].startIdx + 1;
            if (msgCount <= 1) score -= 5.0;

            groups[i].score = score;
        }
    }

    static int estimateMessageTokens(const Message& msg) {
        int total = estimateTokenCount(msg.getText());
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
        return total;
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
            total += estimateMessageTokens(msg);
        }
        return total;
    }
};

} // namespace closecrab
