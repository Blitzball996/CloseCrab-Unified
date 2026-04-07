#include "AgentManager.h"
#include "../utils/UUID.h"
#include "../commands/CommandRegistry.h"
#include "../permissions/PermissionEngine.h"
#include "../memory/MemorySystem.h"
#include <spdlog/spdlog.h>

namespace closecrab {

AgentManager& AgentManager::getInstance() {
    static AgentManager instance;
    return instance;
}

std::vector<std::string> AgentManager::getAllowedTools(AgentType type) const {
    switch (type) {
        case AgentType::EXPLORE:
            return {"Read", "Glob", "Grep", "WebSearch", "WebFetch"};
        case AgentType::PLAN:
            return {"Read", "Glob", "Grep", "WebSearch", "WebFetch", "AskUserQuestion"};
        case AgentType::VERIFICATION:
            return {"Read", "Glob", "Grep", "Bash", "WebSearch"};
        case AgentType::CODE_GUIDE:
            return {"Read", "Glob", "Grep", "WebSearch", "WebFetch"};
        case AgentType::GENERAL_PURPOSE:
        default:
            return {}; // empty = all tools allowed
    }
}

std::string AgentManager::spawnAgent(const AgentConfig& config, APIClient* apiClient,
                                      ToolRegistry* parentToolRegistry, AppState* appState,
                                      const std::string& cwd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto instance = std::make_shared<AgentInstance>();
    instance->id = "a" + std::to_string(++nextId_);
    instance->config = config;
    instance->status = AgentStatus::RUNNING;

    std::string agentId = instance->id;
    agents_[agentId] = instance;

    // Build a filtered tool registry for this agent
    auto allowedTools = getAllowedTools(config.type);

    // Build system prompt for agent
    std::string systemPrompt = "You are a " + agentTypeName(config.type) + " sub-agent. "
        "Complete the task described below. Be thorough and report your findings.";

    if (config.type == AgentType::EXPLORE) {
        systemPrompt += " You have read-only access. Use Glob, Grep, and Read to explore the codebase.";
    } else if (config.type == AgentType::PLAN) {
        systemPrompt += " Design an implementation plan. Do NOT write or edit files.";
    }

    // Launch in thread
    auto agentPtr = instance;
    instance->future = std::async(std::launch::async,
        [agentPtr, apiClient, parentToolRegistry, appState, cwd, systemPrompt, allowedTools, config]() {
            try {
                // Create sub-QueryEngine
                QueryEngineConfig qeConfig;
                qeConfig.cwd = cwd;
                qeConfig.apiClient = apiClient;
                qeConfig.toolRegistry = parentToolRegistry;
                qeConfig.permissionEngine = &PermissionEngine::getInstance();
                qeConfig.appState = appState;
                qeConfig.systemPrompt = systemPrompt;
                qeConfig.maxTurns = config.maxTurns;
                qeConfig.allowedTools = allowedTools; // Filter tools by agent type

                QueryEngine subEngine(qeConfig);

                std::string accumulated;
                QueryCallbacks callbacks;
                callbacks.onText = [&](const std::string& text) {
                    accumulated += text;
                };
                callbacks.onError = [&](const std::string& err) {
                    agentPtr->error = err;
                };
                callbacks.onAskPermission = [](const std::string&, const std::string&) -> bool {
                    return true; // Auto-approve in sub-agents
                };

                subEngine.submitMessage(config.prompt, callbacks);

                agentPtr->output = accumulated;
                agentPtr->status = AgentStatus::COMPLETED;
            } catch (const std::exception& e) {
                agentPtr->error = e.what();
                agentPtr->status = AgentStatus::FAILED;
            }
        }
    );

    spdlog::info("Spawned agent {} ({}): {}", agentId, agentTypeName(config.type),
                 config.prompt.substr(0, 80));
    return agentId;
}

AgentResult AgentManager::getResult(const std::string& agentId, bool block) {
    std::shared_ptr<AgentInstance> instance;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = agents_.find(agentId);
        if (it == agents_.end()) return {agentId, AgentStatus::FAILED, "", "Agent not found"};
        instance = it->second;
    }

    if (block && instance->future.valid()) {
        instance->future.wait();
    }

    return {instance->id, instance->status, instance->output, instance->error};
}

void AgentManager::killAgent(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agents_.find(agentId);
    if (it != agents_.end()) {
        it->second->interrupted = true;
        it->second->status = AgentStatus::KILLED;
    }
}

std::vector<std::pair<std::string, AgentStatus>> AgentManager::listAgents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, AgentStatus>> result;
    for (const auto& [id, inst] : agents_) {
        result.push_back({id, inst->status});
    }
    return result;
}

} // namespace closecrab
