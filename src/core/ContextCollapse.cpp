#include "ContextCollapse.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace closecrab {

int64_t ContextCollapse::collapseOldMessages(std::vector<Message>& messages, int keepRecentCount) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (messages.empty()) return 0;

    int64_t totalSaved = 0;
    int collapseEnd = static_cast<int>(messages.size()) - keepRecentCount;
    if (collapseEnd <= 0) return 0;

    for (int i = 0; i < collapseEnd; i++) {
        auto& msg = messages[i];

        // Skip system/meta messages and already-collapsed messages
        if (msg.isMeta || msg.isCompactSummary) continue;
        if (collapsed_.count(i)) continue;

        // Only collapse messages with substantial content
        std::string fullText;
        for (const auto& block : msg.content) {
            if (block.type == ContentBlockType::TEXT) {
                fullText += block.text;
            } else if (block.type == ContentBlockType::TOOL_USE) {
                fullText += block.toolName + " " + block.toolInput.dump();
            } else if (block.type == ContentBlockType::TOOL_RESULT) {
                fullText += block.toolResult.is_string()
                    ? block.toolResult.get<std::string>()
                    : block.toolResult.dump();
            }
        }

        int64_t originalTokens = estimateTokens(fullText);
        if (originalTokens < 50) continue;  // Not worth collapsing small messages

        // Generate summary and store original
        std::string summary = generateSummary(msg);
        int64_t summaryTokens = estimateTokens(summary);

        CollapsedEntry entry;
        entry.messageIndex = i;
        entry.summary = summary;
        entry.fullContent = fullText;
        entry.originalTokens = originalTokens;
        entry.collapsedTokens = summaryTokens;
        collapsed_[i] = std::move(entry);

        // Replace message content with summary
        msg.content.clear();
        ContentBlock summaryBlock;
        summaryBlock.type = ContentBlockType::TEXT;
        summaryBlock.text = summary;
        msg.content.push_back(std::move(summaryBlock));

        totalSaved += (originalTokens - summaryTokens);
    }

    if (totalSaved > 0) {
        spdlog::info("ContextCollapse: collapsed {} messages, saved ~{} tokens",
                     collapsed_.size(), totalSaved);
    }
    return totalSaved;
}

bool ContextCollapse::expandMessage(std::vector<Message>& messages, int messageIndex) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = collapsed_.find(messageIndex);
    if (it == collapsed_.end()) return false;
    if (messageIndex < 0 || messageIndex >= static_cast<int>(messages.size())) return false;

    // Restore original content as a single text block
    auto& msg = messages[messageIndex];
    msg.content.clear();
    ContentBlock restored;
    restored.type = ContentBlockType::TEXT;
    restored.text = it->second.fullContent;
    msg.content.push_back(std::move(restored));

    spdlog::debug("ContextCollapse: expanded message {} (+{} tokens)",
                  messageIndex, it->second.originalTokens - it->second.collapsedTokens);

    collapsed_.erase(it);
    return true;
}

int64_t ContextCollapse::estimateSavings(const std::vector<Message>& messages, int keepRecentCount) const {
    std::lock_guard<std::mutex> lock(mutex_);

    int64_t potentialSavings = 0;
    int collapseEnd = static_cast<int>(messages.size()) - keepRecentCount;
    if (collapseEnd <= 0) return 0;

    for (int i = 0; i < collapseEnd; i++) {
        const auto& msg = messages[i];
        if (msg.isMeta || msg.isCompactSummary) continue;
        if (collapsed_.count(i)) continue;

        std::string fullText;
        for (const auto& block : msg.content) {
            if (block.type == ContentBlockType::TEXT) {
                fullText += block.text;
            } else if (block.type == ContentBlockType::TOOL_USE) {
                fullText += block.toolName + " " + block.toolInput.dump();
            } else if (block.type == ContentBlockType::TOOL_RESULT) {
                fullText += block.toolResult.is_string()
                    ? block.toolResult.get<std::string>()
                    : block.toolResult.dump();
            }
        }

        int64_t tokens = estimateTokens(fullText);
        if (tokens < 50) continue;

        // Summary is roughly 20-30 tokens
        potentialSavings += (tokens - 25);
    }
    return potentialSavings;
}

void ContextCollapse::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    collapsed_.clear();
}

int ContextCollapse::collapsedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(collapsed_.size());
}

int64_t ContextCollapse::totalTokensSaved() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t total = 0;
    for (const auto& [idx, entry] : collapsed_) {
        total += (entry.originalTokens - entry.collapsedTokens);
    }
    return total;
}

std::string ContextCollapse::generateSummary(const Message& msg) const {
    for (const auto& block : msg.content) {
        if (block.type == ContentBlockType::TOOL_USE) {
            // "[Tool: {name}] {first 50 chars of input}"
            std::string inputStr = block.toolInput.dump();
            if (inputStr.size() > 50) inputStr = inputStr.substr(0, 50) + "...";
            return "[Tool: " + block.toolName + "] " + inputStr;
        }
        if (block.type == ContentBlockType::TOOL_RESULT) {
            // "[Result: {first 50 chars}] ({size} chars)"
            std::string content = block.toolResult.is_string()
                ? block.toolResult.get<std::string>()
                : block.toolResult.dump();
            size_t totalSize = content.size();
            if (content.size() > 50) content = content.substr(0, 50);
            return "[Result: " + content + "] (" + std::to_string(totalSize) + " chars)";
        }
    }

    // For user/assistant text messages: first 80 chars + "..."
    std::string text = msg.getText();
    if (text.size() > 80) {
        return text.substr(0, 80) + "...";
    }
    return text;
}

int64_t ContextCollapse::estimateTokens(const std::string& text) const {
    if (text.empty()) return 0;
    int cjk = 0;
    for (unsigned char c : text) {
        if (c >= 0xE0) cjk++;
    }
    return static_cast<int64_t>((text.size() - cjk) / 4 + cjk / 2 + 1);
}

} // namespace closecrab
