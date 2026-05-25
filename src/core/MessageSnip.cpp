#include "MessageSnip.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <vector>
#include <set>

namespace closecrab {

// Internal helper: represents a snip candidate
struct SnipCandidate {
    size_t messageIdx;
    size_t blockIdx;
    int64_t tokenCount;
    std::string contentPreview;
};

SnipResult MessageSnip::snipLargest(std::vector<Message>& messages,
                                     int64_t targetTokensToFree,
                                     int keepRecentMessages) {
    SnipResult result{0, 0, 0};
    if (messages.empty() || targetTokensToFree <= 0) return result;

    int snipEnd = static_cast<int>(messages.size()) - keepRecentMessages;
    if (snipEnd <= 0) return result;

    // Build list of all snip candidates (content blocks with their sizes)
    std::vector<SnipCandidate> candidates;

    for (int i = 0; i < snipEnd; i++) {
        auto& msg = messages[i];
        if (msg.isMeta || msg.isCompactSummary) continue;

        for (size_t bi = 0; bi < msg.content.size(); bi++) {
            const auto& block = msg.content[bi];
            std::string content;

            if (block.type == ContentBlockType::TOOL_RESULT) {
                content = block.toolResult.is_string()
                    ? block.toolResult.get<std::string>()
                    : block.toolResult.dump();
            } else if (block.type == ContentBlockType::TEXT) {
                content = block.text;
            } else {
                continue;
            }

            int64_t tokens = estimateTokens(content);
            if (tokens < 30) continue;  // Not worth snipping tiny blocks

            SnipCandidate c;
            c.messageIdx = static_cast<size_t>(i);
            c.blockIdx = bi;
            c.tokenCount = tokens;
            c.contentPreview = content.substr(0, 40);
            candidates.push_back(std::move(c));
        }
    }

    // Sort by token count descending (snip largest first)
    std::sort(candidates.begin(), candidates.end(),
              [](const SnipCandidate& a, const SnipCandidate& b) {
                  return a.tokenCount > b.tokenCount;
              });

    // Snip until we've freed enough tokens
    std::set<size_t> affectedMessages;
    for (const auto& candidate : candidates) {
        if (result.tokensFreed >= targetTokensToFree) break;

        auto& block = messages[candidate.messageIdx].content[candidate.blockIdx];
        std::string content;
        if (block.type == ContentBlockType::TOOL_RESULT) {
            content = block.toolResult.is_string()
                ? block.toolResult.get<std::string>()
                : block.toolResult.dump();
        } else {
            content = block.text;
        }

        size_t originalSize = content.size();
        std::string placeholder = "[snipped: " + std::to_string(originalSize) + " chars]";

        if (block.type == ContentBlockType::TOOL_RESULT) {
            block.toolResult = nlohmann::json(placeholder);
        } else {
            block.text = placeholder;
        }

        int64_t freed = candidate.tokenCount - estimateTokens(placeholder);
        result.tokensFreed += freed;
        result.blocksSnipped++;
        affectedMessages.insert(candidate.messageIdx);
    }

    result.messagesAffected = static_cast<int>(affectedMessages.size());

    if (result.blocksSnipped > 0) {
        spdlog::info("MessageSnip: snipped {} blocks in {} messages, freed ~{} tokens",
                     result.blocksSnipped, result.messagesAffected, result.tokensFreed);
    }
    return result;
}

SnipResult MessageSnip::snipOldToolResults(std::vector<Message>& messages,
                                            int keepRecentMessages,
                                            int maxChars) {
    SnipResult result{0, 0, 0};
    if (messages.empty()) return result;

    int snipEnd = static_cast<int>(messages.size()) - keepRecentMessages;
    if (snipEnd <= 0) return result;

    std::set<size_t> affectedMessages;

    for (int i = 0; i < snipEnd; i++) {
        auto& msg = messages[i];
        if (msg.isMeta || msg.isCompactSummary) continue;

        for (auto& block : msg.content) {
            if (block.type != ContentBlockType::TOOL_RESULT) continue;

            std::string content = block.toolResult.is_string()
                ? block.toolResult.get<std::string>()
                : block.toolResult.dump();

            if (static_cast<int>(content.size()) <= maxChars) continue;

            int64_t originalTokens = estimateTokens(content);
            std::string placeholder = "[snipped: " + std::to_string(content.size()) + " chars]";
            block.toolResult = nlohmann::json(placeholder);

            int64_t freed = originalTokens - estimateTokens(placeholder);
            result.tokensFreed += freed;
            result.blocksSnipped++;
            affectedMessages.insert(static_cast<size_t>(i));
        }
    }

    result.messagesAffected = static_cast<int>(affectedMessages.size());

    if (result.blocksSnipped > 0) {
        spdlog::info("MessageSnip: snipped {} old tool results (>{} chars), freed ~{} tokens",
                     result.blocksSnipped, maxChars, result.tokensFreed);
    }
    return result;
}

int64_t MessageSnip::estimateSnipSavings(const std::vector<Message>& messages,
                                          int keepRecentMessages) {
    int64_t savings = 0;
    int snipEnd = static_cast<int>(messages.size()) - keepRecentMessages;
    if (snipEnd <= 0) return 0;

    for (int i = 0; i < snipEnd; i++) {
        const auto& msg = messages[i];
        if (msg.isMeta || msg.isCompactSummary) continue;

        for (const auto& block : msg.content) {
            std::string content;
            if (block.type == ContentBlockType::TOOL_RESULT) {
                content = block.toolResult.is_string()
                    ? block.toolResult.get<std::string>()
                    : block.toolResult.dump();
            } else if (block.type == ContentBlockType::TEXT) {
                content = block.text;
            } else {
                continue;
            }

            int64_t tokens = estimateTokens(content);
            if (tokens > 30) {
                // Placeholder is ~10 tokens
                savings += (tokens - 10);
            }
        }
    }
    return savings;
}

int64_t MessageSnip::estimateTokens(const std::string& text) {
    if (text.empty()) return 0;
    int cjk = 0;
    for (unsigned char c : text) {
        if (c >= 0xE0) cjk++;
    }
    return static_cast<int64_t>((text.size() - cjk) / 4 + cjk / 2 + 1);
}

} // namespace closecrab