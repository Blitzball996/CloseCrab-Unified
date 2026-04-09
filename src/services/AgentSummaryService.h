#pragma once

#include "../core/Message.h"
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {

class AgentSummaryService {
public:
    static AgentSummaryService& getInstance() {
        static AgentSummaryService instance;
        return instance;
    }

    template <typename ApiClient>
    std::string summarize(const std::string& agentId,
                          const std::vector<Message>& messages,
                          ApiClient* apiClient) {
        std::string summary;

        if (apiClient) {
            summary = summarizeWithLLM(agentId, messages, apiClient);
        } else {
            summary = summarizeLocally(agentId, messages);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            summaries_[agentId] = summary;
        }
        spdlog::info("AgentSummary: generated summary for agent '{}'", agentId);
        return summary;
    }

    std::string getSummary(const std::string& agentId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = summaries_.find(agentId);
        if (it != summaries_.end()) return it->second;
        return {};
    }

private:
    AgentSummaryService() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> summaries_;

    template <typename ApiClient>
    std::string summarizeWithLLM(const std::string& agentId,
                                 const std::vector<Message>& messages,
                                 ApiClient* apiClient) {
        // Build a condensed transcript for the LLM
        std::string transcript;
        for (const auto& msg : messages) {
            std::string roleStr = (msg.role == MessageRole::USER) ? "User" :
                                  (msg.role == MessageRole::ASSISTANT) ? "Assistant" : "System";
            transcript += roleStr + ": " + msg.getText() + "\n";
        }

        std::string prompt =
            "Summarize the following agent conversation in 2-3 sentences. "
            "Focus on what was accomplished and any pending items.\n\n"
            "Agent ID: " + agentId + "\n\n" + transcript;

        try {
            return apiClient->complete(prompt);
        } catch (const std::exception& e) {
            spdlog::warn("AgentSummary: LLM summarization failed for '{}': {}", agentId, e.what());
            return summarizeLocally(agentId, messages);
        }
    }

    std::string summarizeLocally(const std::string& agentId,
                                 const std::vector<Message>& messages) {
        int totalMessages = static_cast<int>(messages.size());
        int toolCalls = 0;
        std::string firstUserMsg;
        std::string lastUserMsg;

        for (const auto& msg : messages) {
            // Count tool calls
            for (const auto& block : msg.content) {
                if (block.type == ContentBlockType::TOOL_USE) ++toolCalls;
            }
            // Track user messages
            if (msg.role == MessageRole::USER) {
                std::string text = msg.getText();
                if (!text.empty()) {
                    if (firstUserMsg.empty()) firstUserMsg = text;
                    lastUserMsg = text;
                }
            }
        }

        // Truncate messages for display
        auto truncate = [](const std::string& s, size_t maxLen = 80) -> std::string {
            if (s.size() <= maxLen) return s;
            return s.substr(0, maxLen) + "...";
        };

        std::string summary = "Agent '" + agentId + "': " +
            std::to_string(totalMessages) + " messages, " +
            std::to_string(toolCalls) + " tool calls.";

        if (!firstUserMsg.empty()) {
            summary += " Started with: \"" + truncate(firstUserMsg) + "\".";
        }
        if (!lastUserMsg.empty() && lastUserMsg != firstUserMsg) {
            summary += " Last request: \"" + truncate(lastUserMsg) + "\".";
        }

        return summary;
    }
};

} // namespace closecrab
