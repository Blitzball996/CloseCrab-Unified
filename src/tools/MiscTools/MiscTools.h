#pragma once

#include "../Tool.h"
#include <thread>
#include <chrono>

namespace closecrab {

class SleepTool : public Tool {
public:
    std::string getName() const override { return "Sleep"; }
    std::string getDescription() const override { return "Pause execution for a specified duration."; }
    std::string getCategory() const override { return "workflow"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"seconds",{{"type","integer"},{"description","Duration in seconds"}}}
        }},{"required",{"seconds"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        int seconds = input["seconds"].get<int>();
        if (seconds < 0 || seconds > 300) return ToolResult::fail("Duration must be 0-300 seconds");
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        return ToolResult::ok("Slept for " + std::to_string(seconds) + " seconds");
    }
};

class SendMessageTool : public Tool {
public:
    std::string getName() const override { return "SendMessage"; }
    std::string getDescription() const override {
        return "Send a message to the user or to another agent.";
    }
    std::string getCategory() const override { return "interaction"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"to",{{"type","string"},{"description","Recipient (user or agent ID)"}}},
            {"content",{{"type","string"},{"description","Message content"}}}
        }},{"required",{"to","content"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string to = input["to"].get<std::string>();
        std::string content = input["content"].get<std::string>();
        // For now, just display to user
        if (ctx.onProgress) ctx.onProgress("[Message to " + to + "] " + content);
        return ToolResult::ok("Message sent to " + to);
    }
};

} // namespace closecrab
