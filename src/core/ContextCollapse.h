#pragma once
#include "Message.h"
#include <vector>
#include <map>
#include <string>
#include <mutex>

namespace closecrab {

struct CollapsedEntry {
    int messageIndex;
    std::string summary;        // One-line summary shown to LLM
    std::string fullContent;    // Original full content (stored in memory)
    int64_t originalTokens;     // How many tokens the original was
    int64_t collapsedTokens;    // How many tokens the summary is
};

class ContextCollapse {
public:
    // Collapse messages older than keepRecentCount
    // Returns number of tokens saved
    int64_t collapseOldMessages(std::vector<Message>& messages, int keepRecentCount = 6);

    // Expand a specific collapsed message back to full content
    bool expandMessage(std::vector<Message>& messages, int messageIndex);

    // Check if context collapse would help (returns estimated savings)
    int64_t estimateSavings(const std::vector<Message>& messages, int keepRecentCount = 6) const;

    // Clear all stored expansions (after hard compact)
    void reset();

    // Get stats
    int collapsedCount() const;
    int64_t totalTokensSaved() const;

private:
    std::string generateSummary(const Message& msg) const;
    int64_t estimateTokens(const std::string& text) const;
    std::map<int, CollapsedEntry> collapsed_;
    mutable std::mutex mutex_;
};

} // namespace closecrab
