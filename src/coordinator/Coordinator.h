#pragma once

#include "../core/QueryEngine.h"
#include "../agents/AgentManager.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <spdlog/spdlog.h>

namespace closecrab {

// Coordinator — orchestrates multiple agents to solve complex tasks
// Mirrors JackProAi's coordinator/ module
class Coordinator {
public:
    struct SubTask {
        std::string id;
        std::string description;
        AgentType agentType;
        std::string prompt;
        std::string agentId;        // Assigned agent
        AgentStatus status = AgentStatus::PENDING;
        std::string result;
    };

    struct CoordinatorConfig {
        APIClient* apiClient = nullptr;
        ToolRegistry* toolRegistry = nullptr;
        AppState* appState = nullptr;
        std::string cwd;
        int maxSubTasks = 10;
    };

    explicit Coordinator(const CoordinatorConfig& config) : config_(config) {}

    // Execute a complex task by decomposing and coordinating agents
    std::string execute(const std::string& task, const QueryCallbacks& callbacks) {
        if (!config_.apiClient) return "Error: No API client";

        // Step 1: Use LLM to decompose the task
        if (callbacks.onText) callbacks.onText("Analyzing task...\n");
        auto subtasks = decomposeTask(task);

        if (subtasks.empty()) {
            return "Could not decompose task into subtasks.";
        }

        if (callbacks.onText) {
            callbacks.onText("Decomposed into " + std::to_string(subtasks.size()) + " subtasks:\n");
            for (const auto& st : subtasks) {
                callbacks.onText("  [" + agentTypeName(st.agentType) + "] " + st.description + "\n");
            }
            callbacks.onText("\nExecuting...\n\n");
        }

        // Step 2: Spawn agents for each subtask
        auto& mgr = AgentManager::getInstance();
        for (auto& st : subtasks) {
            AgentConfig ac;
            ac.type = st.agentType;
            ac.prompt = st.prompt;
            ac.maxTurns = 15;
            ac.runInBackground = false;

            st.agentId = mgr.spawnAgent(ac, config_.apiClient, config_.toolRegistry,
                                         config_.appState, config_.cwd);
            st.status = AgentStatus::RUNNING;
        }

        // Step 3: Collect results
        std::string combinedResults;
        for (auto& st : subtasks) {
            auto result = mgr.getResult(st.agentId, true); // blocking wait
            st.status = result.status;
            st.result = result.output;

            if (callbacks.onText) {
                std::string statusStr = (result.status == AgentStatus::COMPLETED) ? "OK" : "FAILED";
                callbacks.onText("[" + statusStr + "] " + st.description + "\n");
            }

            combinedResults += "## " + st.description + "\n\n";
            combinedResults += st.result + "\n\n";
        }

        // Step 4: Synthesize final answer
        if (callbacks.onText) callbacks.onText("\nSynthesizing results...\n\n");
        std::string synthesis = synthesize(task, combinedResults, callbacks);

        return synthesis;
    }

private:
    // Use LLM to break task into subtasks
    std::vector<SubTask> decomposeTask(const std::string& task) {
        std::string prompt =
            "Break this task into 2-5 independent subtasks that can be executed in parallel. "
            "For each subtask, specify:\n"
            "- description: what to do\n"
            "- agent_type: one of [explore, plan, general-purpose, verification, code-guide]\n"
            "- prompt: detailed instructions for the agent\n\n"
            "Respond in JSON array format:\n"
            "[{\"description\": \"...\", \"agent_type\": \"...\", \"prompt\": \"...\"}]\n\n"
            "Task: " + task;

        std::vector<Message> msgs = {Message::makeUser(prompt)};
        ModelConfig mc;
        mc.maxTokens = 2048;
        mc.temperature = 0.3f;

        std::string response = config_.apiClient->chat(msgs,
            "You are a task decomposition assistant. Always respond with valid JSON.", mc);

        // Parse JSON response
        std::vector<SubTask> subtasks;
        try {
            // Find JSON array in response
            auto start = response.find('[');
            auto end = response.rfind(']');
            if (start == std::string::npos || end == std::string::npos) return subtasks;

            auto arr = nlohmann::json::parse(response.substr(start, end - start + 1));
            int id = 0;
            for (const auto& item : arr) {
                if (id >= config_.maxSubTasks) break;
                SubTask st;
                st.id = "st_" + std::to_string(++id);
                st.description = item.value("description", "subtask " + std::to_string(id));
                st.agentType = parseAgentType(item.value("agent_type", "general-purpose"));
                st.prompt = item.value("prompt", st.description);
                subtasks.push_back(std::move(st));
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to parse subtask decomposition: {}", e.what());
        }

        return subtasks;
    }

    // Synthesize final answer from subtask results
    std::string synthesize(const std::string& originalTask,
                           const std::string& results,
                           const QueryCallbacks& callbacks) {
        std::string prompt =
            "Based on the following subtask results, provide a comprehensive answer "
            "to the original task.\n\n"
            "Original task: " + originalTask + "\n\n"
            "Subtask results:\n" + results;

        std::vector<Message> msgs = {Message::makeUser(prompt)};
        ModelConfig mc;
        mc.maxTokens = 4096;
        mc.temperature = 0.5f;

        std::string output;
        config_.apiClient->streamChat(msgs, "Synthesize the results concisely.", mc,
            [&](const StreamEvent& event) {
                if (event.type == StreamEvent::EVT_TEXT) {
                    output += event.content;
                    if (callbacks.onText) callbacks.onText(event.content);
                }
            });

        return output;
    }

    CoordinatorConfig config_;
};

} // namespace closecrab
