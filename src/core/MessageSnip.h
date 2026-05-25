#pragma once
#include "Message.h"
#include <vector>
#include <cstdint>

namespace closecrab {

struct SnipResult {
    int messagesAffected;
    int blocksSnipped;
    int64_t tokensFreed;
};

class MessageSnip {
public:
    // Snip the largest content blocks until we free at least targetTokens
    static SnipResult snipLargest(std::vector<Message>& messages,
                                   int64_t targetTokensToFree,
                                   int keepRecentMessages = 4);

    // Snip all tool_result blocks older than keepRecent that exceed maxChars
    static SnipResult snipOldToolResults(std::vector<Message>& messages,
                                          int keepRecentMessages = 4,
                                          int maxChars = 500);

    // Estimate how many tokens we could free by snipping
    static int64_t estimateSnipSavings(const std::vector<Message>& messages,
                                        int keepRecentMessages = 4);

private:
    static int64_t estimateTokens(const std::string& text);
};

} // namespace closecrab
