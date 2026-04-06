#pragma once

#include "../core/QueryEngine.h"
#include "../core/AppState.h"
#include "../tools/ToolRegistry.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <future>
#include <atomic>

namespace closecrab {

enum class AgentType {
    GENERAL_PURPOSE,    // All tools available
    EXPLORE,            // Read-only: Glob, Grep, Read, WebSearch, WebFetch
    PLAN,               // Read-only + analysis
    VERIFICATION,       // Testing + checking
    CODE_GUIDE          // Search + documentation
};

inline std::string agentTypeName(AgentType t) {
    switch (t) {
        case AgentType::GENERAL_PURPOSE: return "general-purpose";
        case AgentType::EXPLORE: return "explore";
        case AgentType::PLAN: return "plan";
        case AgentType::VERIFICATION: return "verification";
        case AgentType::CODE_GUIDE: return "code-guide";
    }
    return "unknown";
}

inline AgentType parseAgentType(const std::string& s) {
    if (s == "explore" || s == "Explore") return AgentType::EXPLORE;
    if (s == "plan" || s == "Plan") return AgentType::PLAN;
    if (s == "verification") return AgentType::VERIFICATION;
    if (s == "code-guide" || s == "claude-code-guide") return AgentType::CODE_GUIDE;
    return AgentType::GENERAL_PURPOSE;
}

enum class AgentStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    KILLED
};

struct AgentResult {
    std::string agentId;
    AgentStatus status = AgentStatus::PENDING;
    std::string output;
    std::string error;
};

struct AgentConfig {
    AgentType type = AgentType::GENERAL_PURPOSE;
    std::string prompt;
    std::string model;
    int maxTurns = 20;
    bool runInBackground = false;
};

class AgentManager {
public:
    static AgentManager& getInstance();

    // Spawn a new agent, returns agent ID
    std::string spawnAgent(const AgentConfig& config, APIClient* apiClient,
                           ToolRegistry* parentToolRegistry, AppState* appState,
                           const std::string& cwd);

    // Get agent result (blocks if still running and block=true)
    AgentResult getResult(const std::string& agentId, bool block = true);

    // Kill a running agent
    void killAgent(const std::string& agentId);

    // List all agents
    std::vector<std::pair<std::string, AgentStatus>> listAgents() const;

private:
    AgentManager() = default;

    // Filter tools based on agent type
    std::vector<std::string> getAllowedTools(AgentType type) const;

    struct AgentInstance {
        std::string id;
        AgentConfig config;
        AgentStatus status = AgentStatus::PENDING;
        std::string output;
        std::string error;
        std::atomic<bool> interrupted{false};
        std::future<void> future;
    };

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<AgentInstance>> agents_;
    int nextId_ = 0;
};

} // namespace closecrab
