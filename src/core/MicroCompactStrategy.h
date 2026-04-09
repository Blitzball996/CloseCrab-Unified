#pragma once

#include "CompactStrategy.h"
#include "Message.h"
#include <vector>
#include <string>
#include <chrono>
#include <spdlog/spdlog.h>

namespace closecrab {

// Fine-grained incremental compaction — truncates oversized tool results
// and text blocks without removing entire messages.
class MicroCompactStrategy : public CompactStrategy {
public:
    std::string name() const override { return "micro"; }

    bool shouldTrigger(const std::vector<Message>& messages,
                       int estimatedTokens,
                       int maxContextTokens) const override {
        (void)messages;
        return estimatedTokens > static_cast<int>(maxContextTokens * 0.60);
    }

    CompactMetadata compact(std::vector<Message>& messages,
                            APIClient* /*apiClient*/,
                            int /*maxContextTokens*/) override {
        auto t0 = std::chrono::steady_clock::now();

        CompactMetadata meta;
        meta.trigger = "micro";
        meta.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        meta.preTokens = estimateAllTokens(messages);

        const size_t keepRecent = 10;
        size_t scanEnd = (messages.size() > keepRecent)
            ? messages.size() - keepRecent : 0;

        int truncatedBlocks = 0;
        size_t lastTruncIdx = 0;
        for (size_t i = 0; i < scanEnd; i++) {
            auto& msg = messages[i];
            for (auto& block : msg.content) {
                if (block.type == ContentBlockType::TOOL_RESULT) {
                    std::string content = block.toolResult.is_string()
                        ? block.toolResult.get<std::string>()
                        : block.toolResult.dump();
                    if (content.size() > 2000) {
                        std::string truncated = content.substr(0, 500)
                            + "\n...[truncated, was " + std::to_string(content.size()) + " chars]";
                        block.toolResult = truncated;
                        truncatedBlocks++;
                        lastTruncIdx = i;
                    }
                } else if (block.type == ContentBlockType::TEXT) {
                    if (block.text.size() > 3000) {
                        block.text = block.text.substr(0, 800) + "\n...[truncated]";
                        truncatedBlocks++;
                        lastTruncIdx = i;
                    }
                }
            }
        }

        if (truncatedBlocks > 0) {
            // Insert boundary message at the truncation point
            Message boundary = Message::makeSystem(
                SystemSubtype::MICROCOMPACT_BOUNDARY,
                "[Micro-compaction applied: " + std::to_string(truncatedBlocks) + " blocks truncated]");
            boundary.isCompactSummary = true;

            size_t insertPos = std::min(lastTruncIdx + 1, messages.size());
            messages.insert(messages.begin() + static_cast<ptrdiff_t>(insertPos),
                            std::move(boundary));
        }

        meta.messagesSummarized = truncatedBlocks;
        meta.postTokens = estimateAllTokens(messages);
        meta.preservedSegment = "last " + std::to_string(keepRecent) + " messages untouched";

        auto t1 = std::chrono::steady_clock::now();
        meta.durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        spdlog::info("MicroCompact: {} blocks truncated, {} -> {} tokens ({:.1f}ms)",
                     truncatedBlocks, meta.preTokens, meta.postTokens, meta.durationMs);
        return meta;
    }

private:
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
