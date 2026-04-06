#pragma once

#include "../Tool.h"
#include <map>
#include <mutex>
#include <chrono>

namespace closecrab {

// In-memory task store (shared across TaskCreate/Update/Get/List/Stop tools)
struct TaskEntry {
    std::string id;
    std::string subject;
    std::string description;
    std::string status = "pending"; // pending, in_progress, completed, failed
    std::string owner;
    std::vector<std::string> blockedBy;
    std::vector<std::string> blocks;
    int64_t createdAt = 0;
    int64_t updatedAt = 0;
};

class TaskStore {
public:
    static TaskStore& getInstance() {
        static TaskStore instance;
        return instance;
    }

    std::string create(const std::string& subject, const std::string& description) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string id = std::to_string(++nextId_);
        TaskEntry t;
        t.id = id;
        t.subject = subject;
        t.description = description;
        t.createdAt = nowMs();
        t.updatedAt = t.createdAt;
        tasks_[id] = std::move(t);
        return id;
    }

    TaskEntry* get(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        return (it != tasks_.end()) ? &it->second : nullptr;
    }

    std::vector<TaskEntry> list() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TaskEntry> result;
        for (auto& [id, t] : tasks_) result.push_back(t);
        return result;
    }

    bool update(const std::string& id, const nlohmann::json& updates) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return false;
        auto& t = it->second;
        if (updates.contains("status")) t.status = updates["status"].get<std::string>();
        if (updates.contains("subject")) t.subject = updates["subject"].get<std::string>();
        if (updates.contains("description")) t.description = updates["description"].get<std::string>();
        if (updates.contains("owner")) t.owner = updates["owner"].get<std::string>();
        t.updatedAt = nowMs();
        if (updates.value("status", "") == "deleted") { tasks_.erase(it); }
        return true;
    }

private:
    TaskStore() = default;
    static int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    std::mutex mutex_;
    std::map<std::string, TaskEntry> tasks_;
    int nextId_ = 0;
};

// ---- TaskCreateTool ----
class TaskCreateTool : public Tool {
public:
    std::string getName() const override { return "TaskCreate"; }
    std::string getDescription() const override { return "Create a new task for tracking work."; }
    std::string getCategory() const override { return "workflow"; }
    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"subject",{{"type","string"}}},{"description",{{"type","string"}}}
        }},{"required",{"subject","description"}}};
    }
    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto id = TaskStore::getInstance().create(
            input["subject"].get<std::string>(), input["description"].get<std::string>());
        return ToolResult::ok("Task #" + id + " created: " + input["subject"].get<std::string>());
    }
};

// ---- TaskUpdateTool ----
class TaskUpdateTool : public Tool {
public:
    std::string getName() const override { return "TaskUpdate"; }
    std::string getDescription() const override { return "Update a task's status or details."; }
    std::string getCategory() const override { return "workflow"; }
    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"taskId",{{"type","string"}}},{"status",{{"type","string"}}},
            {"subject",{{"type","string"}}},{"description",{{"type","string"}}}
        }},{"required",{"taskId"}}};
    }
    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string id = input["taskId"].get<std::string>();
        if (!TaskStore::getInstance().update(id, input))
            return ToolResult::fail("Task #" + id + " not found");
        return ToolResult::ok("Task #" + id + " updated");
    }
};

// ---- TaskGetTool ----
class TaskGetTool : public Tool {
public:
    std::string getName() const override { return "TaskGet"; }
    std::string getDescription() const override { return "Get details of a specific task."; }
    std::string getCategory() const override { return "workflow"; }
    bool isReadOnly() const override { return true; }
    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{{"taskId",{{"type","string"}}}}},{"required",{"taskId"}}};
    }
    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto* t = TaskStore::getInstance().get(input["taskId"].get<std::string>());
        if (!t) return ToolResult::fail("Task not found");
        nlohmann::json j = {{"id",t->id},{"subject",t->subject},{"description",t->description},
            {"status",t->status},{"owner",t->owner}};
        return ToolResult::ok(j.dump(2), j);
    }
};

// ---- TaskListTool ----
class TaskListTool : public Tool {
public:
    std::string getName() const override { return "TaskList"; }
    std::string getDescription() const override { return "List all tasks."; }
    std::string getCategory() const override { return "workflow"; }
    bool isReadOnly() const override { return true; }
    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{}}};
    }
    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto tasks = TaskStore::getInstance().list();
        std::string out;
        for (const auto& t : tasks) {
            std::string icon = (t.status == "completed") ? "[x]" :
                               (t.status == "in_progress") ? "[~]" : "[ ]";
            out += "#" + t.id + " " + icon + " " + t.subject + "\n";
        }
        if (out.empty()) out = "No tasks.\n";
        return ToolResult::ok(out);
    }
};

// ---- TaskStopTool ----
class TaskStopTool : public Tool {
public:
    std::string getName() const override { return "TaskStop"; }
    std::string getDescription() const override { return "Stop a running task by its ID."; }
    std::string getCategory() const override { return "workflow"; }
    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"task_id",{{"type","string"},{"description","Task ID to stop"}}}
        }},{"required",{"task_id"}}};
    }
    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string id = input["task_id"].get<std::string>();
        auto* t = TaskStore::getInstance().get(id);
        if (!t) return ToolResult::fail("Task #" + id + " not found");
        if (t->status != "in_progress" && t->status != "pending") {
            return ToolResult::fail("Task #" + id + " is not running (status: " + t->status + ")");
        }
        TaskStore::getInstance().update(id, {{"status", "killed"}});
        return ToolResult::ok("Task #" + id + " stopped.");
    }
};

// ---- TaskOutputTool ----
class TaskOutputTool : public Tool {
public:
    std::string getName() const override { return "TaskOutput"; }
    std::string getDescription() const override { return "Get the output of a task."; }
    std::string getCategory() const override { return "workflow"; }
    bool isReadOnly() const override { return true; }
    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"task_id",{{"type","string"},{"description","Task ID"}}},
            {"block",{{"type","boolean"},{"description","Wait for completion (default true)"}}},
            {"timeout",{{"type","integer"},{"description","Max wait time in ms"}}}
        }},{"required",{"task_id"}}};
    }
    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string id = input["task_id"].get<std::string>();
        auto* t = TaskStore::getInstance().get(id);
        if (!t) return ToolResult::fail("Task #" + id + " not found");

        nlohmann::json data = {
            {"id", t->id}, {"status", t->status},
            {"subject", t->subject}, {"description", t->description}
        };
        return ToolResult::ok("Task #" + id + " [" + t->status + "]: " + t->description, data);
    }
};

} // namespace closecrab
