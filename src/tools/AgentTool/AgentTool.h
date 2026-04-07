#pragma once

#include "../Tool.h"
#include "../../agents/AgentManager.h"

namespace closecrab {

class AgentTool : public Tool {
public:
    std::string getName() const override { return "Agent"; }
    std::string getDescription() const override {
        return "Launch a sub-agent to handle complex tasks. Types: general-purpose, explore, plan, verification, code-guide.";
    }
    std::string getCategory() const override { return "agent"; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"description", {{"type", "string"}, {"description", "Short description of the task"}}},
                {"prompt", {{"type", "string"}, {"description", "Detailed task for the agent"}}},
                {"subagent_type", {{"type", "string"}, {"description", "Agent type: general-purpose, explore, plan, verification, code-guide"}}},
                {"run_in_background", {{"type", "boolean"}, {"description", "Run in background (default false)"}}}
            }},
            {"required", {"description", "prompt"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        AgentConfig config;
        config.type = parseAgentType(input.value("subagent_type", "general-purpose"));
        config.prompt = input["prompt"].get<std::string>();
        config.runInBackground = input.value("run_in_background", false);
        config.maxTurns = 20;

        auto& mgr = AgentManager::getInstance();

        if (!ctx.appState) return ToolResult::fail("No app state available");
        if (!ctx.apiClient) return ToolResult::fail("No API client available for sub-agent");

        std::string agentId = mgr.spawnAgent(config, ctx.apiClient, ctx.toolRegistry, ctx.appState, ctx.cwd);

        if (config.runInBackground) {
            return ToolResult::ok("Agent " + agentId + " launched in background (" +
                                  agentTypeName(config.type) + ")");
        }

        // Wait for result
        auto result = mgr.getResult(agentId, true);
        if (result.status == AgentStatus::COMPLETED) {
            return ToolResult::ok(result.output);
        }
        return ToolResult::fail("Agent failed: " + result.error);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Agent (" + input.value("subagent_type", "general") + "): " +
               input.value("description", "task");
    }
};

} // namespace closecrab
