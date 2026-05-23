#pragma once
#include <string>
#include <vector>
#include "../core/Message.h"

namespace closecrab {

class CompactSummary {
public:
    // Generate a brief summary of messages being compacted
    static std::string summarize(const std::vector<Message>& messages, int fromIdx, int toIdx) {
        std::string summary = "Conversation summary (messages " + std::to_string(fromIdx + 1) +
                              "-" + std::to_string(toIdx + 1) + "):\n";

        int userMsgs = 0, assistantMsgs = 0, toolCalls = 0;
        std::string firstUserMsg, lastUserMsg;

        for (int i = fromIdx; i <= toIdx && i < (int)messages.size(); i++) {
            const auto& msg = messages[i];
            if (msg.role == MessageRole::USER) {
                userMsgs++;
                std::string text = msg.getText();
                if (firstUserMsg.empty() && !text.empty()) {
                    firstUserMsg = text.substr(0, 80);
                }
                if (!text.empty()) lastUserMsg = text.substr(0, 80);
            } else if (msg.role == MessageRole::ASSISTANT) {
                assistantMsgs++;
                for (const auto& block : msg.content) {
                    if (block.type == ContentBlockType::TOOL_USE) toolCalls++;
                }
            }
        }

        summary += "- " + std::to_string(userMsgs) + " user messages, " +
                   std::to_string(assistantMsgs) + " assistant responses, " +
                   std::to_string(toolCalls) + " tool calls\n";
        if (!firstUserMsg.empty()) summary += "- Started with: \"" + firstUserMsg + "\"\n";
        if (!lastUserMsg.empty() && lastUserMsg != firstUserMsg) {
            summary += "- Ended with: \"" + lastUserMsg + "\"\n";
        }

        return summary;
    }
};

} // namespace closecrab
