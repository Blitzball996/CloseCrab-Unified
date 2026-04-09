#pragma once

#include "CompactStrategy.h"
#include "Message.h"
#include "../api/APIClient.h"
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace closecrab {

// Smart auto-compaction strategy — mirrors HistoryCompactor logic
// with the CompactStrategy interface for pluggable use.
class AutoCompactStrategy : public CompactStrategy {
public:
    std::string name() const override { return "auto"; }

    bool shouldTrigger(const std::vector<Message>& messages,
                       int estimatedTokens,
                       int maxContextTokens) const override {
        (void)messages;
        return estimatedTokens > static_cast<int>(maxContextTokens * 0.75);
    }

    CompactMetadata compact(std::vector<Message>& messages,
                            APIClient* apiClient,
                            int maxContextTokens) override {
        auto t0 = std::chrono::steady_clock::now();

        CompactMetadata meta;
        meta.trigger = "auto";
        meta.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        meta.preTokens = estimateAllTokens(messages);

        const int keepRecent = 10;
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

        // Try LLM summary first, fall back to local
        std::string summary;
        if (apiClient && !apiClient->isLocal()) {
            summary = buildLLMSummary(messages, keepStart, apiClient);
        }
        if (summary.empty()) {
            summary = buildLocalSummary(messages, keepStart);
        }

        // Create compact boundary message
        Message compactMsg = Message::makeSystem(
            SystemSubtype::COMPACT_BOUNDARY,
            "[Conversation history was compressed. Summary of earlier messages:]\n\n" + summary);
        compactMsg.isCompactSummary = true;

        // Replace old messages with [compact_msg] + [recent messages]
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

        spdlog::info("AutoCompact: {} msgs summarized, {} -> {} tokens ({:.1f}ms)",
                     meta.messagesSummarized, meta.preTokens, meta.postTokens, meta.durationMs);
        return meta;
    }

private:
    // Don't split between assistant tool_use and user tool_result
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

    // Quick token estimate (~4 chars/token for English, ~2 for CJK)
    static int estimateTokenCount(const std::string& text) {
        if (text.empty()) return 0;
        int cjk = 0;
        for (unsigned char c : text) {
            if (c >= 0xE0) cjk++; // rough CJK lead-byte count
        }
        int ascii = static_cast<int>(text.size()) - cjk;
        return ascii / 4 + cjk / 2;
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
    // Fast local summary without LLM call
    static std::string buildLocalSummary(const std::vector<Message>& messages, size_t upTo) {
        std::string summary;
        int userCount = 0, assistantCount = 0, toolCount = 0;

        for (size_t i = 0; i < upTo; i++) {
            const auto& msg = messages[i];
            if (msg.role == MessageRole::USER) {
                userCount++;
                std::string text = msg.getText();
                if (!text.empty() && text.size() > 10) {
                    if (userCount == 1 || i == upTo - 1 || i == upTo - 2) {
                        std::string truncated = text.substr(0, 200);
                        if (text.size() > 200) truncated += "...";
                        summary += "- User: " + truncated + "\n";
                    }
                }
            } else if (msg.role == MessageRole::ASSISTANT) {
                assistantCount++;
                for (const auto& block : msg.content) {
                    if (block.type == ContentBlockType::TOOL_USE) toolCount++;
                }
            }
        }

        summary += "\n[" + std::to_string(userCount) + " user messages, "
                 + std::to_string(assistantCount) + " assistant responses, "
                 + std::to_string(toolCount) + " tool calls were compressed]";
        return summary;
    }

    // LLM-based summary (better quality, costs tokens)
    static std::string buildLLMSummary(const std::vector<Message>& messages,
                                        size_t upTo, APIClient* apiClient) {
        std::string oldContent;
        for (size_t i = 0; i < upTo && oldContent.size() < 8000; i++) {
            const auto& msg = messages[i];
            std::string role = (msg.role == MessageRole::USER) ? "User" : "Assistant";
            std::string text = msg.getText();
            if (text.size() > 300) text = text.substr(0, 300) + "...";
            if (!text.empty()) {
                oldContent += role + ": " + text + "\n";
            }
        }
        if (oldContent.empty()) return "";

        std::string prompt =
            "Summarize this conversation history in 2-3 concise paragraphs. "
            "Focus on: what the user asked for, what was accomplished, "
            "key decisions made, and any important context for continuing.\n\n"
            + oldContent;

        try {
            std::vector<Message> summaryMsgs = {Message::makeUser(prompt)};
            ModelConfig cfg;
            cfg.maxTokens = 500;
            cfg.temperature = 0.3f;
            return apiClient->chat(summaryMsgs, "You are a conversation summarizer.", cfg);
        } catch (...) {
            spdlog::warn("AutoCompact: LLM summary failed, using local summary");
            return "";
        }
    }
};

} // namespace closecrab
