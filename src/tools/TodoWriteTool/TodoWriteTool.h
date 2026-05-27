#pragma once

#include "../Tool.h"
#include <fstream>
#include <filesystem>

namespace closecrab {

class TodoWriteTool : public Tool {
public:
    std::string getName() const override { return "TodoWrite"; }
    std::string getDescription() const override {
        return "Use this tool to create and manage a structured task list for your current coding session. "
               "This helps track progress, organize complex tasks, and demonstrate thoroughness. "
               "Use proactively for: multi-step tasks (3+ steps), non-trivial work, user-provided task lists. "
               "Mark a task as in_progress BEFORE beginning, completed when fully done. "
               "Each todo MUST be an object with fields: content (string), status (pending|in_progress|completed), "
               "activeForm (string, present-continuous form like \"Running tests\"). "
               "Do NOT pass plain strings — every item must be a JSON object with these three fields.";
    }
    std::string getCategory() const override { return "workflow"; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"todos", {
                    {"type", "array"},
                    {"description", "The updated todo list. Each item is an object with content/status/activeForm."},
                    {"items", {
                        {"type", "object"},
                        {"properties", {
                            {"content", {{"type", "string"}, {"description", "Task description"}}},
                            {"status", {{"type", "string"}, {"enum", {"pending", "in_progress", "completed"}}}},
                            {"activeForm", {{"type", "string"}, {"description", "Present-continuous form (e.g. 'Running tests')"}}}
                        }},
                        {"required", {"content", "status", "activeForm"}}
                    }}
                }}
            }},
            {"required", {"todos"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        if (!input.contains("todos") || !input["todos"].is_array()) {
            return ToolResult::fail("Missing or invalid 'todos' field — must be an array");
        }
        auto todos = input["todos"];

        std::string display;
        size_t itemIdx = 0;
        for (const auto& todo : todos) {
            itemIdx++;
            // Defensive type check: model sometimes passes strings instead of objects
            if (!todo.is_object()) {
                return ToolResult::fail(
                    "todos[" + std::to_string(itemIdx - 1) + "] is not an object. "
                    "Each todo must be {\"content\": \"...\", \"status\": \"pending|in_progress|completed\", \"activeForm\": \"...\"}");
            }
            std::string content = todo.contains("content") && todo["content"].is_string()
                ? todo["content"].get<std::string>() : "";
            std::string status = todo.contains("status") && todo["status"].is_string()
                ? todo["status"].get<std::string>() : "pending";
            std::string icon = (status == "completed") ? "[x]" :
                               (status == "in_progress") ? "[~]" : "[ ]";
            display += icon + " " + content + "\n";
        }

        // Save to .crab/todos.json
        namespace fs = std::filesystem;
        fs::path todoPath = fs::path(ctx.cwd) / ".crab" / "todos.json";
        if (!fs::exists(todoPath.parent_path())) fs::create_directories(todoPath.parent_path());

        std::ofstream f(todoPath);
        f << input.dump(2);

        // JackProAi: return acknowledgement message to nudge model to continue
        std::string msg = display + "\nTodos have been modified successfully. "
                          "Continue using the todo list to track progress.";
        return ToolResult::ok(msg, input);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        if (input.contains("todos") && input["todos"].is_array()) {
            return "Update " + std::to_string(input["todos"].size()) + " todo(s)";
        }
        return "Update todo list";
    }
};

} // namespace closecrab
