#pragma once

#include "../Tool.h"
#include <fstream>
#include <filesystem>

namespace closecrab {

class TodoWriteTool : public Tool {
public:
    std::string getName() const override { return "TodoWrite"; }
    std::string getDescription() const override {
        return "Create or update a structured task/todo list for tracking progress.";
    }
    std::string getCategory() const override { return "workflow"; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"todos", {
                    {"type", "array"},
                    {"items", {
                        {"type", "object"},
                        {"properties", {
                            {"id", {{"type", "string"}}},
                            {"content", {{"type", "string"}}},
                            {"status", {{"type", "string"}, {"description", "pending, in_progress, completed"}}},
                            {"priority", {{"type", "string"}, {"description", "high, medium, low"}}}
                        }}
                    }}
                }}
            }},
            {"required", {"todos"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto todos = input["todos"];

        std::string display;
        for (const auto& todo : todos) {
            std::string status = todo.value("status", "pending");
            std::string icon = (status == "completed") ? "[x]" :
                               (status == "in_progress") ? "[~]" : "[ ]";
            display += icon + " " + todo.value("content", "") + "\n";
        }

        // Save to .claude/todos.json
        namespace fs = std::filesystem;
        fs::path todoPath = fs::path(ctx.cwd) / ".claude" / "todos.json";
        if (!fs::exists(todoPath.parent_path())) fs::create_directories(todoPath.parent_path());

        std::ofstream f(todoPath);
        f << input.dump(2);

        return ToolResult::ok(display, input);
    }
};

} // namespace closecrab
