#pragma once

#include "Message.h"
#include "../api/APIClient.h"
#include <vector>
#include <string>
#include <spdlog/spdlog.h>

namespace closecrab {

// HistoryCompactor — compresses old messages when approaching context limits
// Mirrors JackProAi's SnipCompact service
class HistoryCompactor {
public:
    struct Config {
        int maxContextTokens = 128000;   // Model's context window
        float compactThreshold = 0.75f;  // Trigger at 75% usage
        int keepRecentMessages = 10;     // Always keep last N messages
        int summaryMaxTokens = 500;      // Max tokens for summary
    };

    explicit HistoryCompactor(const Config& config = {}) : config_(config) {}

    // Check if compaction is needed and perform it
    // Returns true if messages were compacted
    bool compactIfNeeded(std::vector<Message>& messages, APIClient* apiClient) {
        if (!apiClient || messages.size() <= static_cast<size_t>(config_.keepRecentMessages + 2)) {
            return false;
        }

        int totalTokens = estimateTokens(messages, apiClient);
        int threshold = static_cast<int>(config_.maxContextTokens * config_.compactThreshold);

        if (totalTokens < threshold) return false;

        spdlog::info("History compaction triggered: {} tokens (threshold: {})", totalTokens, threshold);
        return performCompaction(messages, apiClient);
    }

    // Force compaction regardless of token count
    bool forceCompact(std::vector<Message>& messages, APIClient* apiClient) {
        if (!apiClient || messages.size() <= static_cast<size_t>(config_.keepRecentMessages + 2)) {
            return false;
        }
        return performCompaction(messages, apiClient);
    }

    void setMaxContextTokens(int tokens) { config_.maxContextTokens = tokens; }

private:
    int estimateTokens(const std::vector<Message>& messages, APIClient* apiClient) const {
        int total = 0;
        for (const auto& msg : messages) {
            total += apiClient->countTokens(msg.getText());
            // Tool use/result blocks add overhead
            for (const auto& block : msg.content) {
                if (block.type == ContentBlockType::TOOL_USE) {
                    total += apiClient->countTokens(block.toolInput.dump());
                } else if (block.type == ContentBlockType::TOOL_RESULT) {
                    total += apiClient->countTokens(
                        block.toolResult.is_string()
                            ? block.toolResult.get<std::string>()
                            : block.toolResult.dump());
                }
            }
        }
        return total;
    }

    bool performCompaction(std::vector<Message>& messages, APIClient* apiClient) {
        // Split: old messages to summarize + recent messages to keep
        size_t keepStart = messages.size() - std::min(
            messages.size(),
            static_cast<size_t>(config_.keepRecentMessages));

        // Ensure we don't split in the middle of a tool_use/tool_result pair
        keepStart = adjustSplitPoint(messages, keepStart);

        if (keepStart <= 1) return false; // Nothing to compact

        // Build summary of old messages
        std::string summary = buildLocalSummary(messages, keepStart);

        // Try LLM-based summary if API is available and not local
        if (!apiClient->isLocal()) {
            std::string llmSummary = buildLLMSummary(messages, keepStart, apiClient);
            if (!llmSummary.empty()) summary = llmSummary;
        }

        // Create compact boundary message
        Message compactMsg;
        compactMsg.type = MessageType::SYSTEM;
        compactMsg.role = MessageRole::SYSTEM;
        compactMsg.content.push_back({ContentBlockType::TEXT,
            "[Conversation history was compressed. Summary of earlier messages:]\n\n" + summary});

        // Replace old messages with summary
        std::vector<Message> newMessages;
        newMessages.push_back(std::move(compactMsg));
        for (size_t i = keepStart; i < messages.size(); i++) {
            newMessages.push_back(std::move(messages[i]));
        }

        messages = std::move(newMessages);
        spdlog::info("Compacted history: kept {} messages + summary", messages.size());
        return true;
    }

    // Don't split between assistant tool_use and user tool_result
    size_t adjustSplitPoint(const std::vector<Message>& messages, size_t point) const {
        if (point >= messages.size()) return point;
        // If the message at split point is a tool result, move back to include the tool_use
        if (point > 0 && messages[point].isToolUseResult) {
            point--;
        }
        // If it's an assistant message with tool_use, include the next tool_result too
        if (point < messages.size() - 1) {
            for (const auto& block : messages[point].content) {
                if (block.type == ContentBlockType::TOOL_USE) {
                    point++; // Skip past the tool_result
                    break;
                }
            }
        }
        return point;
    }

    // Fast local summary without LLM call
    std::string buildLocalSummary(const std::vector<Message>& messages, size_t upTo) const {
        std::string summary;
        int userCount = 0, assistantCount = 0, toolCount = 0;

        for (size_t i = 0; i < upTo; i++) {
            const auto& msg = messages[i];
            if (msg.role == MessageRole::USER) {
                userCount++;
                std::string text = msg.getText();
                if (!text.empty() && text.size() > 10) {
                    // Keep first user message and last user message before split
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
    std::string buildLLMSummary(const std::vector<Message>& messages, size_t upTo,
                                 APIClient* apiClient) const {
        // Build a condensed version of old messages for the LLM to summarize
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

        std::string prompt = "Summarize this conversation history in 2-3 concise paragraphs. "
                             "Focus on: what the user asked for, what was accomplished, "
                             "key decisions made, and any important context for continuing.\n\n"
                             + oldContent;

        try {
            std::vector<Message> summaryMsgs = {Message::makeUser(prompt)};
            ModelConfig cfg;
            cfg.maxTokens = config_.summaryMaxTokens;
            cfg.temperature = 0.3f;
            return apiClient->chat(summaryMsgs, "You are a conversation summarizer.", cfg);
        } catch (...) {
            spdlog::warn("LLM summary failed, using local summary");
            return "";
        }
    }

    Config config_;
};

} // namespace closecrab
