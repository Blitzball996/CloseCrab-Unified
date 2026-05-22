#pragma once

#include "../Tool.h"
#include "../TaskTools/TaskTools.h"
#include <thread>
#include <chrono>

namespace closecrab {

/// Reads output from a background task, optionally blocking until completion.
class TaskOutputTool : public Tool {
public:
    std::string getName() const override { return "TaskOutput"; }
    std::string getDescription() const override {
        return "Read output from a background task. Blocks until done by default.";
    }
    std::string getCategory() const override { return "workflow"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {
            {"task_id", {{"type", "string"}, {"description", "Task ID to read output from"}}},
            {"block", {{"type", "boolean"}, {"description", "Wait for task completion (default true)"}}},
            {"timeout", {{"type", "integer"}, {"description", "Max wait ms (default 30000)"}}}
        }}, {"required", {"task_id"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string id = input["task_id"].get<std::string>();
        bool block = input.value("block", true);
        int timeoutMs = input.value("timeout", 30000);

        auto& store = TaskStore::getInstance();
        auto* t = store.get(id);
        if (!t) return ToolResult::fail("Task #" + id + " not found");

        if (block && (t->status == "pending" || t->status == "in_progress")) {
            int elapsed = 0;
            constexpr int pollMs = 200;
            while (elapsed < timeoutMs) {
                std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
                elapsed += pollMs;
                t = store.get(id);
                if (!t) return ToolResult::fail("Task #" + id + " disappeared");
                if (t->status != "pending" && t->status != "in_progress") break;
            }
        }

        t = store.get(id);
        if (!t) return ToolResult::fail("Task #" + id + " not found");

        nlohmann::json data = {
            {"id", t->id}, {"status", t->status},
            {"subject", t->subject}, {"output", t->description}
        };
        return ToolResult::ok(
            "Task #" + id + " [" + t->status + "]: " + t->description, data);
    }
};

} // namespace closecrab
