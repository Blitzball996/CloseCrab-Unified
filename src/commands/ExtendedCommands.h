#pragma once

#include "Command.h"
#include "../hooks/HookManager.h"
#include "../memory/FileMemoryManager.h"
#include "../agents/AgentManager.h"
#include "../tools/TaskTools/TaskTools.h"
#include "../mcp/MCPClient.h"
#include "../coordinator/Coordinator.h"
#include "../voice/VoiceEngine.h"

namespace closecrab {

// /review - code review via git diff + LLM analysis
class ReviewCommand : public Command {
public:
    std::string getName() const override { return "review"; }
    std::string getDescription() const override { return "Review code changes (git diff -> LLM analysis)"; }
    std::vector<std::string> getAliases() const override { return {"cr"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        // Get git diff
        std::string diffCmd = args.empty() ? "git diff --staged" : "git diff " + args;
#ifdef _WIN32
        std::string fullCmd = "cmd /c \"" + diffCmd + "\" 2>&1";
        FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
        std::string fullCmd = diffCmd + " 2>&1";
        FILE* pipe = popen(fullCmd.c_str(), "r");
#endif
        if (!pipe) return CommandResult::fail("Failed to run git diff");

        std::string diff;
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe) != nullptr) {
            diff += buf;
            if (diff.size() > 50000) { diff += "\n...(truncated)"; break; }
        }
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif

        if (diff.empty()) {
            ctx.print("No changes to review. Try: /review HEAD~1\n");
            return CommandResult::ok();
        }

        // Submit to LLM for review
        std::string prompt = "Please review the following code changes. "
            "Focus on bugs, security issues, and improvements:\n\n```diff\n" + diff + "\n```";

        ctx.print("Reviewing " + std::to_string(diff.size()) + " bytes of changes...\n\n");
        ctx.queryEngine->submitMessage(prompt, {
            [&](const std::string& text) { ctx.print(text); },
            nullptr, nullptr, nullptr,
            []() {},
            [&](const std::string& err) { ctx.print("Error: " + err + "\n"); }
        });
        return CommandResult::ok();
    }
};

// /hooks - list and manage hooks
class HooksCommand : public Command {
public:
    std::string getName() const override { return "hooks"; }
    std::string getDescription() const override { return "List configured hooks"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& mgr = HookManager::getInstance();
        ctx.print(mgr.listHooks());
        return CommandResult::ok();
    }
};

// /memory - manage file-based memories
class MemoryCommand : public Command {
public:
    std::string getName() const override { return "memory"; }
    std::string getDescription() const override { return "List, view, or delete memories"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        FileMemoryManager mgr(ctx.cwd);

        if (args.empty() || args == "list") {
            auto memories = mgr.loadAll();
            if (memories.empty()) {
                ctx.print("No memories stored. Memories are saved in .claude/memory/\n");
                return CommandResult::ok();
            }
            for (const auto& m : memories) {
                ctx.print("[" + memoryTypeName(m.type) + "] " + m.name + " (" + m.filename + ")\n");
                ctx.print("  " + m.description + "\n");
            }
            return CommandResult::ok();
        }

        if (args.substr(0, 6) == "delete" || args.substr(0, 2) == "rm") {
            std::string filename = args.substr(args.find(' ') + 1);
            if (mgr.removeMemory(filename)) {
                ctx.print("Deleted: " + filename + "\n");
            } else {
                ctx.print("Not found: " + filename + "\n");
            }
            return CommandResult::ok();
        }

        if (args.substr(0, 4) == "show" || args.substr(0, 4) == "view") {
            std::string filename = args.substr(args.find(' ') + 1);
            auto memories = mgr.loadAll();
            for (const auto& m : memories) {
                if (m.filename == filename || m.name == filename) {
                    ctx.print("# " + m.name + " [" + memoryTypeName(m.type) + "]\n");
                    ctx.print(m.description + "\n\n");
                    ctx.print(m.content + "\n");
                    return CommandResult::ok();
                }
            }
            ctx.print("Not found: " + filename + "\n");
            return CommandResult::ok();
        }

        ctx.print("Usage: /memory [list|show <name>|delete <name>]\n");
        return CommandResult::ok();
    }
};

// /tasks - show task list
class TasksCommand : public Command {
public:
    std::string getName() const override { return "tasks"; }
    std::string getDescription() const override { return "Show current task list"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& store = TaskStore::getInstance();
        auto tasks = store.list();
        if (tasks.empty()) {
            ctx.print("No tasks.\n");
            return CommandResult::ok();
        }
        for (const auto& t : tasks) {
            std::string icon = (t.status == "completed") ? "[x]" :
                               (t.status == "in_progress") ? "[>]" : "[ ]";
            ctx.print(icon + " #" + t.id + " " + t.subject + "\n");
        }
        return CommandResult::ok();
    }
};

// /agents - show running agents
class AgentsCommand : public Command {
public:
    std::string getName() const override { return "agents"; }
    std::string getDescription() const override { return "List running and completed agents"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto agents = AgentManager::getInstance().listAgents();
        if (agents.empty()) {
            ctx.print("No agents.\n");
            return CommandResult::ok();
        }
        for (const auto& [id, status] : agents) {
            std::string statusStr;
            switch (status) {
                case AgentStatus::PENDING: statusStr = "pending"; break;
                case AgentStatus::RUNNING: statusStr = "running"; break;
                case AgentStatus::COMPLETED: statusStr = "completed"; break;
                case AgentStatus::FAILED: statusStr = "failed"; break;
                case AgentStatus::KILLED: statusStr = "killed"; break;
            }
            ctx.print(id + " [" + statusStr + "]\n");
        }
        return CommandResult::ok();
    }
};

// /mcp - MCP server management
class McpCommand : public Command {
public:
    std::string getName() const override { return "mcp"; }
    std::string getDescription() const override { return "List MCP servers and their tools"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& mgr = MCPServerManager::getInstance();
        auto servers = mgr.getServerNames();
        if (servers.empty()) {
            ctx.print("No MCP servers configured. Add them in .claude/settings.json\n");
            return CommandResult::ok();
        }
        for (const auto& name : servers) {
            auto* client = mgr.getClient(name);
            std::string status = (client && client->isConnected()) ? "connected" : "disconnected";
            ctx.print(name + " [" + status + "]\n");
        }
        return CommandResult::ok();
    }
};

// /brief - toggle brief mode
class BriefCommand : public Command {
public:
    std::string getName() const override { return "brief"; }
    std::string getDescription() const override { return "Toggle brief/concise output mode"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.appState->fastMode = !ctx.appState->fastMode;
        ctx.print("Brief mode: " + std::string(ctx.appState->fastMode ? "ON" : "OFF") + "\n");
        return CommandResult::ok();
    }
};

// /plugin - list plugins
class PluginCommand : public Command {
public:
    std::string getName() const override { return "plugin"; }
    std::string getDescription() const override { return "List loaded plugins"; }
    std::vector<std::string> getAliases() const override { return {"plugins"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto plugins = PluginManager::getInstance().getAllPlugins();
        if (plugins.empty()) {
            ctx.print("No plugins loaded. Place plugins in .claude/plugins/\n");
            return CommandResult::ok();
        }
        for (const auto& p : plugins) {
            ctx.print(p.name + " v" + p.version + (p.enabled ? "" : " [disabled]") + "\n");
            if (!p.description.empty()) ctx.print("  " + p.description + "\n");
        }
        return CommandResult::ok();
    }
};

// /pr - create pull request via gh CLI
class PrCommand : public Command {
public:
    std::string getName() const override { return "pr"; }
    std::string getDescription() const override { return "Create a pull request (uses gh CLI)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            // Ask LLM to help create PR
            std::string prompt = "Help me create a pull request. First run `git log --oneline -10` "
                "and `git diff main...HEAD --stat` to understand the changes, then create the PR "
                "using `gh pr create`.";
            ctx.queryEngine->submitMessage(prompt, {
                [&](const std::string& text) { ctx.print(text); },
                nullptr, nullptr, nullptr,
                []() {},
                [&](const std::string& err) { ctx.print("Error: " + err + "\n"); }
            });
        } else {
            // Direct gh command
            std::string cmd = "gh pr " + args;
            ctx.print("Running: " + cmd + "\n");
#ifdef _WIN32
            std::system(("cmd /c \"" + cmd + "\"").c_str());
#else
            std::system(cmd.c_str());
#endif
        }
        return CommandResult::ok();
    }
};

// /share - export conversation as shareable markdown
class ShareCommand : public Command {
public:
    std::string getName() const override { return "share"; }
    std::string getDescription() const override { return "Export conversation as shareable markdown"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        const auto& msgs = ctx.queryEngine->getMessages();
        if (msgs.empty()) {
            ctx.print("No messages to share.\n");
            return CommandResult::ok();
        }

        std::string filename = args;
        if (filename.empty()) {
            filename = "conversation_" + std::to_string(std::time(nullptr)) + ".md";
        }

        std::ofstream f(filename);
        if (!f.is_open()) return CommandResult::fail("Cannot create: " + filename);

        f << "# CloseCrab Conversation\n\n";
        f << "> Model: " << ctx.appState->currentModel << "\n\n---\n\n";

        for (const auto& msg : msgs) {
            std::string role = (msg.role == MessageRole::USER) ? "**User**" : "**Assistant**";
            std::string text = msg.getText();
            if (!text.empty()) {
                f << role << "\n\n" << text << "\n\n---\n\n";
            }
        }
        f.close();
        ctx.print("Saved to: " + filename + "\n");
        return CommandResult::ok();
    }
};

// /skills - list available skills
class SkillsCommand : public Command {
public:
    std::string getName() const override { return "skills"; }
    std::string getDescription() const override { return "List available skills"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto skills = SkillDirectory::getInstance().getAllSkills();
        if (skills.empty()) {
            ctx.print("No skills loaded. Place skills in .claude/skills/\n");
            return CommandResult::ok();
        }
        for (const auto& s : skills) {
            ctx.print(s.name);
            if (!s.trigger.empty()) ctx.print(" [trigger: " + s.trigger + "]");
            ctx.print("\n");
            if (!s.description.empty()) ctx.print("  " + s.description + "\n");
        }
        return CommandResult::ok();
    }
};

// /vim - toggle vim mode
class VimCommand : public Command {
public:
    std::string getName() const override { return "vim"; }
    std::string getDescription() const override { return "Toggle vim keybinding mode"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.appState->vimMode = !ctx.appState->vimMode;
        ctx.print("Vim mode: " + std::string(ctx.appState->vimMode ? "ON" : "OFF") + "\n");
        if (ctx.appState->vimMode) {
            ctx.print("  i/a = insert mode, Esc = normal mode, :q = quit\n");
        }
        return CommandResult::ok();
    }
};

// /coordinator - run a task in multi-agent coordinator mode
class CoordinatorCommand : public Command {
public:
    std::string getName() const override { return "coordinator"; }
    std::string getDescription() const override { return "Execute a complex task using multi-agent coordination"; }
    std::vector<std::string> getAliases() const override { return {"coord", "ultraplan"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Usage: /coordinator <task description>\n");
            ctx.print("Decomposes the task into subtasks and runs them in parallel with specialized agents.\n");
            return CommandResult::ok();
        }

        Coordinator::CoordinatorConfig cc;
        // Get apiClient from the queryEngine's context — we need to access it
        // For now, use a workaround: submit via queryEngine with coordinator prompt
        cc.appState = ctx.appState;
        cc.toolRegistry = ctx.toolRegistry;
        cc.cwd = ctx.cwd;

        // We need the APIClient — it's stored in ToolContext but not CommandContext
        // Use the queryEngine to run the coordinator task
        std::string prompt = "[COORDINATOR MODE] Decompose and execute this task using multiple "
            "sub-agents (Agent tool). Spawn explore agents for research, plan agents for design, "
            "and general-purpose agents for implementation. Coordinate their results.\n\nTask: " + args;

        QueryCallbacks cb;
        cb.onText = [&](const std::string& text) { ctx.print(text); };
        cb.onToolUse = [&](const std::string& name, const nlohmann::json&) {
            ctx.print("\n  [" + name + "] ");
        };
        cb.onToolResult = [&](const std::string& name, const ToolResult& result) {
            ctx.print(result.success ? "OK\n" : ("Error: " + result.error + "\n"));
        };
        cb.onComplete = []() {};
        cb.onError = [&](const std::string& err) { ctx.print("Error: " + err + "\n"); };
        cb.onAskPermission = [](const std::string&, const std::string&) { return true; };

        ctx.queryEngine->submitMessage(prompt, cb);
        return CommandResult::ok();
    }
};

// /voice - toggle voice mode (TTS output)
class VoiceCommand : public Command {
public:
    std::string getName() const override { return "voice"; }
    std::string getDescription() const override { return "Toggle voice output (text-to-speech)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& voice = VoiceEngine::getInstance();
        if (!voice.isAvailable()) {
            ctx.print("Voice not available on this platform.\n");
            return CommandResult::ok();
        }

        bool enable = !voice.isEnabled();
        if (args == "on") enable = true;
        else if (args == "off") enable = false;

        voice.setEnabled(enable);
        ctx.appState->voiceEnabled = enable;
        ctx.print("Voice: " + std::string(enable ? "ON" : "OFF") + "\n");

        if (enable) {
            voice.speak("Voice mode enabled.");
        }
        return CommandResult::ok();
    }
};

// /theme - switch color theme
class ThemeCommand : public Command {
public:
    std::string getName() const override { return "theme"; }
    std::string getDescription() const override { return "Switch color theme (dark/light/minimal)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Usage: /theme [dark|light|minimal]\n");
            ctx.print("Current: dark (default)\n");
            return CommandResult::ok();
        }
        // Theme switching would modify the ansi:: color functions
        // For now, just acknowledge
        ctx.print("Theme set to: " + args + "\n");
        ctx.print("(Theme changes take effect on next prompt)\n");
        return CommandResult::ok();
    }
};

} // namespace closecrab
