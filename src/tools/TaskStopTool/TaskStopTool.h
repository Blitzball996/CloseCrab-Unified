#pragma once

#include "../Tool.h"
#include "../TaskTools/TaskTools.h"

namespace closecrab {

/// Stops a running task by marking it as "stopped" in the TaskStore.
class TaskStopTool : public Tool {
public:
    std::string getName() const override { return "TaskStop"; }
    std::string getDescription() const override {
        return "Stop a running task by its ID.";
    }
    std::string getCategory() const override { return "workflow"; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {
            {"task_id", {{"type", "string"}, {"description", "Task ID to stop"}}}
        }}, {"required", {"task_id"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string id = input["task_id"].get<std::string>();
        auto& store = TaskStore::getInstance();
        auto* t = store.get(id);
        if (!t) return ToolResult::fail("Task #" + id + " not found");

        if (t->status != "in_progress" && t->status != "pending") {
            return ToolResult::fail(
                "Task #" + id + " is not running (status: " + t->status + ")");
        }

        store.update(id, {{"status", "stopped"}});
        return ToolResult::ok("Task #" + id + " stopped.");
    }
};

} // namespace closecrab
