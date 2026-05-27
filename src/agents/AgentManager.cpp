#include "AgentManager.h"
#include "../utils/UUID.h"
#include "../commands/CommandRegistry.h"
#include "../permissions/PermissionEngine.h"
#include "../memory/MemorySystem.h"
#include "../services/AgentProgress.h"
#include <spdlog/spdlog.h>
#include <condition_variable>

namespace closecrab {

namespace {
// Cap concurrent sub-agent threads so a turn that spawns many background agents
// can't explode the thread/connection count (a prime crash cause). Mirrors the
// 4-5 concurrent agents observed in JackProAi.
class AgentSemaphore {
public:
    explicit AgentSemaphore(int count) : count_(count) {}
    void acquire() {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [this] { return count_ > 0; });
        --count_;
    }
    void release() {
        std::lock_guard<std::mutex> lock(m_);
        ++count_;
        cv_.notify_one();
    }
private:
    std::mutex m_;
    std::condition_variable cv_;
    int count_;
};
AgentSemaphore& agentSlots() {
    static AgentSemaphore sem(4); // max 4 concurrent sub-agents
    return sem;
}

// One concise activity line per sub-agent tool event (task B: visibility).
std::string toolActivityLine(const std::string& name, const nlohmann::json& input) {
    if (name == "Read" || name == "Write" || name == "Edit") {
        return name + " " + input.value("file_path", "");
    } else if (name == "Bash" || name == "PowerShell") {
        std::string cmd = input.value("command", "");
        if (cmd.size() > 60) cmd = cmd.substr(0, 60) + "...";
        return name + " " + cmd;
    } else if (name == "Glob" || name == "Grep") {
        return name + " " + input.value("pattern", "");
    }
    return name;
}
} // namespace

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
        systemPrompt += " You have read-only access. Use Glob, Grep, and Read to explore the codebase."
                        " IMPORTANT: Keep responses concise. Only read first 50 lines of files (use limit parameter)."
                        " Summarize findings in 200 words or less.";
    } else if (config.type == AgentType::PLAN) {
        systemPrompt += " Design an implementation plan. Do NOT write or edit files.";
    }

    // Launch in thread
    auto agentPtr = instance;
    instance->future = std::async(std::launch::async,
        [agentPtr, apiClient, parentToolRegistry, appState, cwd, systemPrompt, allowedTools, config]() {
            agentSlots().acquire();
            struct SlotGuard { ~SlotGuard() { agentSlots().release(); } } slotGuard;
            try {
                // Create sub-QueryEngine
                QueryEngineConfig qeConfig;
                qeConfig.cwd = cwd;
                qeConfig.apiClient = apiClient;
                qeConfig.toolRegistry = parentToolRegistry;
                qeConfig.permissionEngine = &PermissionEngine::getInstance();
                qeConfig.appState = appState;
                qeConfig.systemPrompt = systemPrompt;
                // Limit agent turns to prevent context bloat (proxy rejects >30KB)
                qeConfig.maxTurns = (config.type == AgentType::EXPLORE) ? 3 : config.maxTurns;
                qeConfig.allowedTools = allowedTools;
                qeConfig.tokenBudget.toolResultBudget = 800;
                // Recursive-spawn guard: sub-agents cannot launch their own agents.
                qeConfig.allowSubagents = false;

                QueryEngine subEngine(qeConfig);

                std::string accumulated;
                int turnsCompleted = 0;
                int toolUseCount = 0;
                const std::string aid = agentPtr->id;
                QueryCallbacks callbacks;
                callbacks.onText = [&](const std::string& text) {
                    accumulated += text;
                    // Update progress after each text chunk (approximates turn completion)
                    turnsCompleted++;
                    int pct = std::min(95, (turnsCompleted * 100) / config.maxTurns);
                    AgentProgress::getInstance().setProgress(aid, pct, "processing");
                };
                // Task B: surface what the sub-agent is doing to the main UI.
                callbacks.onToolUse = [&, aid](const std::string& name, const nlohmann::json& input) {
                    toolUseCount++;
                    std::string activity = toolActivityLine(name, input);
                    AgentProgress::getInstance().setProgress(aid,
                        AgentProgress::getInstance().getProgress(aid), activity);
                    AgentActivitySink::getInstance().log(aid, activity);
                };
                callbacks.onToolResult = [&, aid](const std::string& name, const ToolResult& result) {
                    std::string suffix = result.success ? "done" : ("error: " + result.error);
                    AgentActivitySink::getInstance().log(aid, "  " + name + " -> " + suffix);
                };
                callbacks.onError = [&](const std::string& err) {
                    agentPtr->error = err;
                };
                callbacks.onRetry = [aid](int attempt, int maxAttempts, int /*delayMs*/, const std::string& reason) {
                    AgentActivitySink::getInstance().log(aid,
                        "retrying " + std::to_string(attempt) + "/" + std::to_string(maxAttempts) +
                        " (" + reason + ")");
                };
                callbacks.onAskPermission = [](const std::string&, const std::string&) -> bool {
                    return true; // Auto-approve in sub-agents
                };

                AgentProgress::getInstance().setProgress(aid, 5, "starting");
                subEngine.submitMessage(config.prompt, callbacks);

                agentPtr->output = accumulated;
                agentPtr->status = AgentStatus::COMPLETED;
                AgentProgress::getInstance().setProgress(aid, 100, "done");
            } catch (const std::exception& e) {
                agentPtr->error = e.what();
                agentPtr->status = AgentStatus::FAILED;
                AgentProgress::getInstance().setProgress(agentPtr->id, 100, "failed");
            } catch (...) {
                agentPtr->error = "Unknown exception in agent";
                agentPtr->status = AgentStatus::FAILED;
                AgentProgress::getInstance().setProgress(agentPtr->id, 100, "failed");
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
        // JackProAi: AbortController + timeout. C++ equivalent: wait_for with 300s limit.
        auto status = instance->future.wait_for(std::chrono::seconds(300));
        if (status == std::future_status::timeout) {
            instance->status = AgentStatus::FAILED;
            instance->error = "Agent timed out after 300s";
            instance->interrupted = true;
            spdlog::warn("Agent {} timed out after 300s", agentId);
        }
    }

    return {instance->id, instance->status, instance->output, instance->error};
}

void AgentManager::cancelAgent(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agents_.find(agentId);
    if (it != agents_.end()) {
        it->second->interrupted = true;
        it->second->status = AgentStatus::FAILED;
        it->second->error = "Cancelled by user";
        spdlog::info("Agent {} cancelled", agentId);
    }
}

void AgentManager::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, inst] : agents_) {
        if (inst->future.valid()) {
            inst->interrupted = true;
            auto status = inst->future.wait_for(std::chrono::seconds(5));
            if (status == std::future_status::timeout) {
                spdlog::warn("Agent {} did not finish during cleanup", id);
            }
        }
    }
    agents_.clear();
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
