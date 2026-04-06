#pragma once

#include "../Tool.h"
#include <filesystem>
#include <fstream>

namespace closecrab {

// EnterPlanModeTool — switch to read-only exploration mode
class EnterPlanModeTool : public Tool {
public:
    std::string getName() const override { return "EnterPlanMode"; }
    std::string getDescription() const override {
        return "Enter plan mode for read-only exploration before implementing.";
    }
    std::string getCategory() const override { return "workflow"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json&) override {
        if (!ctx.appState) return ToolResult::fail("No app state");
        ctx.appState->planMode = true;
        return ToolResult::ok("Entered plan mode. Only read-only tools are available. "
                              "Use ExitPlanMode when your plan is ready.");
    }
};

// ExitPlanModeTool — exit plan mode, present plan for approval
class ExitPlanModeTool : public Tool {
public:
    std::string getName() const override { return "ExitPlanMode"; }
    std::string getDescription() const override {
        return "Exit plan mode and present the plan for user approval.";
    }
    std::string getCategory() const override { return "workflow"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json&) override {
        if (!ctx.appState) return ToolResult::fail("No app state");
        ctx.appState->planMode = false;
        return ToolResult::ok("Exited plan mode. All tools are now available.");
    }
};

// EnterWorktreeTool — create isolated git worktree
class EnterWorktreeTool : public Tool {
public:
    std::string getName() const override { return "EnterWorktree"; }
    std::string getDescription() const override {
        return "Create an isolated git worktree for safe experimentation.";
    }
    std::string getCategory() const override { return "workflow"; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {
            {"name", {{"type", "string"}, {"description", "Worktree name (optional)"}}}
        }}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
        std::string name = input.value("name", "wt-" + std::to_string(std::time(nullptr)));

        fs::path wtDir = fs::path(ctx.cwd) / ".claude" / "worktrees" / name;
        std::string branchName = "worktree/" + name;

        // Create worktree
        std::string cmd = "git worktree add \"" + wtDir.string() + "\" -b " + branchName + " HEAD";
#ifdef _WIN32
        std::system(("cmd /c \"" + cmd + "\"").c_str());
#else
        std::system(cmd.c_str());
#endif

        if (!fs::exists(wtDir)) {
            return ToolResult::fail("Failed to create worktree at " + wtDir.string());
        }

        return ToolResult::ok("Created worktree: " + wtDir.string() + " (branch: " + branchName + ")");
    }
};

// ExitWorktreeTool — leave worktree
class ExitWorktreeTool : public Tool {
public:
    std::string getName() const override { return "ExitWorktree"; }
    std::string getDescription() const override { return "Exit and optionally remove a git worktree."; }
    std::string getCategory() const override { return "workflow"; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {
            {"action", {{"type", "string"}, {"description", "keep or remove"}}},
            {"discard_changes", {{"type", "boolean"}, {"description", "Force remove with uncommitted changes"}}}
        }}, {"required", {"action"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string action = input["action"].get<std::string>();
        if (action == "remove") {
            bool force = input.value("discard_changes", false);
            std::string cmd = "git worktree remove \"" + ctx.cwd + "\"";
            if (force) cmd += " --force";
#ifdef _WIN32
            std::system(("cmd /c \"" + cmd + "\"").c_str());
#else
            std::system(cmd.c_str());
#endif
            return ToolResult::ok("Worktree removed.");
        }
        return ToolResult::ok("Worktree kept at: " + ctx.cwd);
    }
};

} // namespace closecrab
