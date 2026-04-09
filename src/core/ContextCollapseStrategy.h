#pragma once

#include "CompactStrategy.h"
#include "Message.h"
#include <vector>
#include <string>
#include <chrono>
#include <unordered_set>
#include <spdlog/spdlog.h>

namespace closecrab {

// Collapse redundant read/search tool results into brief summaries.
// Targets sequences of 3+ consecutive read/search results in older messages.
class ContextCollapseStrategy : public CompactStrategy {
public:
    std::string name() const override { return "collapse"; }

    bool shouldTrigger(const std::vector<Message>& messages,
                       int /*estimatedTokens*/,
                       int /*maxContextTokens*/) const override {
        const size_t keepRecent = 10;
        size_t scanEnd = (messages.size() > keepRecent)
            ? messages.size() - keepRecent : 0;

        // Check for 3+ consecutive read/search tool results
        int consecutive = 0;
        for (size_t i = 0; i < scanEnd; i++) {
            if (isReadOrSearchResult(messages, i)) {
                consecutive++;
                if (consecutive >= 3) return true;
            } else {
                consecutive = 0;
            }
        }
        return false;
    }

    CompactMetadata compact(std::vector<Message>& messages,
                            APIClient* /*apiClient*/,
                            int /*maxContextTokens*/) override {
        auto t0 = std::chrono::steady_clock::now();

        CompactMetadata meta;
        meta.trigger = "collapse";
        meta.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        meta.preTokens = estimateAllTokens(messages);
        const size_t keepRecent = 10;
        size_t scanEnd = (messages.size() > keepRecent)
            ? messages.size() - keepRecent : 0;

        int collapsedCount = 0;

        // Find and collapse sequences of 3+ consecutive read/search results
        size_t i = 0;
        while (i < scanEnd) {
            // Find start of a consecutive sequence
            if (!isReadOrSearchResult(messages, i)) {
                i++;
                continue;
            }

            // Count consecutive read/search results
            size_t seqStart = i;
            size_t seqLen = 0;
            size_t totalBytes = 0;
            bool isSearch = false;

            while (i < scanEnd && isReadOrSearchResult(messages, i)) {
                seqLen++;
                // Measure the tool result content size
                for (const auto& block : messages[i].content) {
                    if (block.type == ContentBlockType::TOOL_RESULT) {
                        std::string content = block.toolResult.is_string()
                            ? block.toolResult.get<std::string>()
                            : block.toolResult.dump();
                        totalBytes += content.size();
                    }
                }
                // Check if any are search tools
                if (i > 0) {
                    for (const auto& block : messages[i - 1].content) {
                        if (block.type == ContentBlockType::TOOL_USE &&
                            isSearchTool(block.toolName)) {
                            isSearch = true;
                        }
                    }
                }
                i++;
            }

            if (seqLen < 3) continue;

            // Collapse the TOOL_RESULT content in this sequence
            // Keep TOOL_USE blocks intact
            for (size_t j = seqStart; j < seqStart + seqLen; j++) {
                for (auto& block : messages[j].content) {
                    if (block.type == ContentBlockType::TOOL_RESULT) {
                        if (isSearch) {
                            block.toolResult = "[collapsed: " + std::to_string(seqLen)
                                + " search results]";
                        } else {
                            block.toolResult = "[collapsed: read " + std::to_string(seqLen)
                                + " files, ~" + std::to_string(totalBytes / 1024) + " KB total]";
                        }
                        collapsedCount++;
                    }
                }
            }
        }

        meta.messagesSummarized = collapsedCount;
        meta.postTokens = estimateAllTokens(messages);
        meta.preservedSegment = "tool_use blocks preserved, results collapsed";

        auto t1 = std::chrono::steady_clock::now();
        meta.durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        spdlog::info("ContextCollapse: {} results collapsed, {} -> {} tokens ({:.1f}ms)",
                     collapsedCount, meta.preTokens, meta.postTokens, meta.durationMs);
        return meta;
    }

private:
    static bool isReadOrSearchTool(const std::string& toolName) {
        static const std::unordered_set<std::string> tools = {
            "Read", "Glob", "Grep", "WebSearch", "WebFetch", "LSP"
        };
        return tools.count(toolName) > 0;
    }

    static bool isSearchTool(const std::string& toolName) {
        return toolName == "Grep" || toolName == "Glob"
            || toolName == "WebSearch" || toolName == "WebFetch";
    }

    // Check if message at idx is a tool_result whose preceding tool_use was read/search
    static bool isReadOrSearchResult(const std::vector<Message>& messages, size_t idx) {
        const auto& msg = messages[idx];
        if (!msg.isToolUseResult) return false;

        // Look for the matching tool_use in the preceding message
        if (idx == 0) return false;
        const auto& prev = messages[idx - 1];
        for (const auto& block : prev.content) {
            if (block.type == ContentBlockType::TOOL_USE &&
                isReadOrSearchTool(block.toolName)) {
                return true;
            }
        }
        return false;
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
