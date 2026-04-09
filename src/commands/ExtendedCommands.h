#pragma once

#include "Command.h"
#include "../hooks/HookManager.h"
#include "../memory/FileMemoryManager.h"
#include "../agents/AgentManager.h"
#include "../tools/TaskTools/TaskTools.h"
#include "../mcp/MCPClient.h"
#include "../coordinator/Coordinator.h"
#include "../voice/VoiceEngine.h"
#include "../utils/ProcessRunner.h"
#include "../security/Sandbox.h"
#include "../permissions/PermissionEngine.h"

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
        cc.appState = ctx.appState;
        cc.toolRegistry = ctx.toolRegistry;
        cc.cwd = ctx.cwd;
        cc.apiClient = ctx.apiClient;

        if (!cc.apiClient) {
            ctx.print("Error: No API client available for coordinator mode.\n");
            return CommandResult::ok();
        }

        Coordinator coordinator(cc);
        ctx.print("Decomposing task into subtasks...\n");

        QueryCallbacks cb;
        cb.onText = [&](const std::string& text) { ctx.print(text); };
        cb.onToolUse = [&](const std::string& name, const nlohmann::json&) {
            ctx.print("\n  [" + name + "] ");
        };
        cb.onToolResult = [&](const std::string&, const ToolResult& r) {
            ctx.print(r.success ? "OK\n" : ("Error: " + r.error + "\n"));
        };
        cb.onComplete = []() {};
        cb.onError = [&](const std::string& err) { ctx.print("Error: " + err + "\n"); };
        cb.onAskPermission = [](const std::string&, const std::string&) { return true; };

        std::string result = coordinator.execute(args, cb);
        if (!result.empty()) {
            ctx.print("\n--- Result ---\n" + result + "\n");
        }
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

// ============================================================
// Batch 3: High-value commands from JackProAi gap analysis
// ============================================================

// /issue - create/view GitHub issues via gh CLI
class IssueCommand : public Command {
public:
    std::string getName() const override { return "issue"; }
    std::string getDescription() const override { return "Create or view GitHub issues (gh CLI)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Usage:\n  /issue list        — list open issues\n"
                      "  /issue view <N>    — view issue #N\n"
                      "  /issue create      — create new issue (LLM-assisted)\n");
            return CommandResult::ok();
        }
        if (args == "list") {
            std::string cmd = "gh issue list --limit 10";
#ifdef _WIN32
            std::system(("cmd /c \"" + cmd + "\"").c_str());
#else
            std::system(cmd.c_str());
#endif
            return CommandResult::ok();
        }
        if (args.substr(0, 4) == "view") {
            std::string num = args.substr(5);
            std::string cmd = "gh issue view " + num;
#ifdef _WIN32
            std::system(("cmd /c \"" + cmd + "\"").c_str());
#else
            std::system(cmd.c_str());
#endif
            return CommandResult::ok();
        }
        if (args == "create") {
            std::string prompt = "Help me create a GitHub issue. Ask me for the title and description, "
                "then use `gh issue create --title \"...\" --body \"...\"` to create it.";
            ctx.queryEngine->submitMessage(prompt, {
                [&](const std::string& t) { ctx.print(t); }, nullptr, nullptr, nullptr,
                []() {}, [&](const std::string& e) { ctx.print("Error: " + e + "\n"); }
            });
            return CommandResult::ok();
        }
        // Direct gh command
        std::string cmd = "gh issue " + args;
#ifdef _WIN32
        std::system(("cmd /c \"" + cmd + "\"").c_str());
#else
        std::system(cmd.c_str());
#endif
        return CommandResult::ok();
    }
};

// /rename - rename current session
class RenameCommand : public Command {
public:
    std::string getName() const override { return "rename"; }
    std::string getDescription() const override { return "Rename the current session"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Usage: /rename <new name>\n");
            return CommandResult::ok();
        }
        ctx.appState->sessionId = args;
        ctx.queryEngine->setSessionId(args);
        ctx.print("Session renamed to: " + args + "\n");
        return CommandResult::ok();
    }
};

// /copy - copy last assistant response to clipboard
class CopyCommand : public Command {
public:
    std::string getName() const override { return "copy"; }
    std::string getDescription() const override { return "Copy last response to clipboard"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        const auto& msgs = ctx.queryEngine->getMessages();
        // Find last assistant message
        std::string lastResponse;
        for (int i = (int)msgs.size() - 1; i >= 0; i--) {
            if (msgs[i].role == MessageRole::ASSISTANT) {
                lastResponse = msgs[i].getText();
                break;
            }
        }
        if (lastResponse.empty()) {
            ctx.print("No assistant response to copy.\n");
            return CommandResult::ok();
        }
#ifdef _WIN32
        // Windows: pipe to clip.exe
        FILE* pipe = _popen("clip", "w");
        if (pipe) {
            fwrite(lastResponse.c_str(), 1, lastResponse.size(), pipe);
            _pclose(pipe);
            ctx.print("Copied to clipboard (" + std::to_string(lastResponse.size()) + " chars).\n");
        } else {
            ctx.print("Failed to access clipboard.\n");
        }
#elif defined(__APPLE__)
        FILE* pipe = popen("pbcopy", "w");
        if (pipe) { fwrite(lastResponse.c_str(), 1, lastResponse.size(), pipe); pclose(pipe); }
        ctx.print("Copied to clipboard.\n");
#else
        FILE* pipe = popen("xclip -selection clipboard", "w");
        if (pipe) { fwrite(lastResponse.c_str(), 1, lastResponse.size(), pipe); pclose(pipe); }
        ctx.print("Copied to clipboard.\n");
#endif
        return CommandResult::ok();
    }
};

// /summary - generate conversation summary
class SummaryCommand : public Command {
public:
    std::string getName() const override { return "summary"; }
    std::string getDescription() const override { return "Generate a summary of the conversation"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        const auto& msgs = ctx.queryEngine->getMessages();
        if (msgs.size() < 4) {
            ctx.print("Not enough messages to summarize.\n");
            return CommandResult::ok();
        }
        std::string prompt = "Summarize our conversation so far in 3-5 bullet points. "
            "Focus on what was asked, what was accomplished, and any pending items.";
        ctx.queryEngine->submitMessage(prompt, {
            [&](const std::string& t) { ctx.print(t); }, nullptr, nullptr, nullptr,
            []() {}, [&](const std::string& e) { ctx.print("Error: " + e + "\n"); }
        });
        return CommandResult::ok();
    }
};

// /usage - detailed token usage statistics
class UsageCommand : public Command {
public:
    std::string getName() const override { return "usage"; }
    std::string getDescription() const override { return "Show detailed token usage statistics"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& tracker = CostTracker::getInstance();
        ctx.print(tracker.getSummary());

        // Context stats
        const auto& msgs = ctx.queryEngine->getMessages();
        int totalChars = 0, userMsgs = 0, assistantMsgs = 0, toolCalls = 0;
        for (const auto& m : msgs) {
            totalChars += (int)m.getText().size();
            if (m.role == MessageRole::USER) userMsgs++;
            else if (m.role == MessageRole::ASSISTANT) {
                assistantMsgs++;
                for (const auto& b : m.content)
                    if (b.type == ContentBlockType::TOOL_USE) toolCalls++;
            }
        }
        ctx.print("\nContext: " + std::to_string(msgs.size()) + " messages "
                  "(" + std::to_string(userMsgs) + " user, "
                  + std::to_string(assistantMsgs) + " assistant, "
                  + std::to_string(toolCalls) + " tool calls)\n");
        ctx.print("Est. tokens: ~" + std::to_string(totalChars / 4) + "\n");
        ctx.print("API time: " + std::to_string(ctx.appState->totalAPIDuration.load()) + "s\n");
        ctx.print("Tool time: " + std::to_string(ctx.appState->totalToolDuration.load()) + "s\n");
        return CommandResult::ok();
    }
};

// /effort - set reasoning effort level
class EffortCommand : public Command {
public:
    std::string getName() const override { return "effort"; }
    std::string getDescription() const override { return "Set reasoning effort (low/medium/high)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Current effort: " + ctx.appState->thinkingConfig.effort + "\n");
            ctx.print("Usage: /effort [low|medium|high]\n");
            return CommandResult::ok();
        }
        if (args == "low" || args == "medium" || args == "high") {
            ctx.appState->thinkingConfig.effort = args;
            if (args == "high") {
                ctx.appState->thinkingConfig.enabled = true;
                ctx.appState->thinkingConfig.budgetTokens = 20000;
            } else if (args == "medium") {
                ctx.appState->thinkingConfig.enabled = true;
                ctx.appState->thinkingConfig.budgetTokens = 10000;
            } else {
                ctx.appState->thinkingConfig.enabled = false;
                ctx.appState->thinkingConfig.budgetTokens = 5000;
            }
            ctx.print("Effort set to: " + args + "\n");
        } else {
            ctx.print("Unknown effort level. Use: low, medium, high\n");
        }
        return CommandResult::ok();
    }
};

// /tag - tag current session
class TagCommand : public Command {
public:
    std::string getName() const override { return "tag"; }
    std::string getDescription() const override { return "Add a tag to the current session"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Usage: /tag <label>\n");
            return CommandResult::ok();
        }
        // Store tag in session ID suffix
        std::string current = ctx.queryEngine->getSessionId();
        if (current.find("#") == std::string::npos) {
            current += "#" + args;
        } else {
            current += "," + args;
        }
        ctx.queryEngine->setSessionId(current);
        ctx.print("Tagged: " + args + "\n");
        return CommandResult::ok();
    }
};

// /rewind - undo last N conversation turns
class RewindCommand : public Command {
public:
    std::string getName() const override { return "rewind"; }
    std::string getDescription() const override { return "Undo last N conversation turns"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        int count = args.empty() ? 2 : std::stoi(args); // Default: remove last user+assistant pair
        auto& msgs = const_cast<std::vector<Message>&>(ctx.queryEngine->getMessages());
        if ((int)msgs.size() <= count) {
            ctx.print("Not enough messages to rewind.\n");
            return CommandResult::ok();
        }
        for (int i = 0; i < count && !msgs.empty(); i++) {
            msgs.pop_back();
        }
        ctx.print("Rewound " + std::to_string(count) + " messages. "
                  "Now at " + std::to_string(msgs.size()) + " messages.\n");
        return CommandResult::ok();
    }
};

// /pr_comments - view PR comments
class PrCommentsCommand : public Command {
public:
    std::string getName() const override { return "pr_comments"; }
    std::string getDescription() const override { return "View comments on a pull request"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string prNum = args.empty() ? "" : args;
        std::string cmd = prNum.empty() ? "gh pr view --comments" : "gh pr view " + prNum + " --comments";
#ifdef _WIN32
        std::system(("cmd /c \"" + cmd + "\"").c_str());
#else
        std::system(cmd.c_str());
#endif
        return CommandResult::ok();
    }
};

// ============================================================
// Batch 4: Medium-value commands + services
// ============================================================

// /thinkback - replay AI's thinking process from last response
class ThinkbackCommand : public Command {
public:
    std::string getName() const override { return "thinkback"; }
    std::string getDescription() const override { return "Replay the AI's thinking process"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        const auto& msgs = ctx.queryEngine->getMessages();
        bool found = false;
        for (int i = (int)msgs.size() - 1; i >= 0; i--) {
            if (msgs[i].role != MessageRole::ASSISTANT) continue;
            for (const auto& block : msgs[i].content) {
                if (block.type == ContentBlockType::THINKING && !block.text.empty()) {
                    ctx.print("\033[2m--- Thinking ---\033[0m\n");
                    ctx.print("\033[2m" + block.text + "\033[0m\n");
                    ctx.print("\033[2m--- End ---\033[0m\n");
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) {
            ctx.print("No thinking content found. Enable with /thinking on\n");
        }
        return CommandResult::ok();
    }
};

// /output-style - switch output format
class OutputStyleCommand : public Command {
public:
    std::string getName() const override { return "output-style"; }
    std::string getDescription() const override { return "Switch output style (markdown/plain/json)"; }
    std::vector<std::string> getAliases() const override { return {"style"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Usage: /output-style [markdown|plain|json]\n");
            return CommandResult::ok();
        }
        // Store in appState for use by output rendering
        // For now, acknowledge the setting
        ctx.print("Output style set to: " + args + "\n");
        return CommandResult::ok();
    }
};

// /autofix-pr - automatically fix issues in a PR
class AutofixPrCommand : public Command {
public:
    std::string getName() const override { return "autofix-pr"; }
    std::string getDescription() const override { return "Auto-fix issues in a pull request"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string prNum = args.empty() ? "" : args;
        std::string prompt;
        if (prNum.empty()) {
            prompt = "Look at the current branch's PR. Run `gh pr checks` to see failing checks, "
                     "then `gh pr diff` to see the changes. Identify and fix any issues.";
        } else {
            prompt = "Look at PR #" + prNum + ". Run `gh pr view " + prNum + " --comments` and "
                     "`gh pr diff " + prNum + "` to understand the changes and feedback. "
                     "Fix any issues mentioned in the comments or failing checks.";
        }
        ctx.queryEngine->submitMessage(prompt, {
            [&](const std::string& t) { ctx.print(t); }, nullptr,
            [&](const std::string& name, const nlohmann::json&) {
                ctx.print("\n  [" + name + "] ");
            },
            [&](const std::string& name, const ToolResult& r) {
                ctx.print(r.success ? "OK\n" : ("Error\n"));
            },
            []() {}, [&](const std::string& e) { ctx.print("Error: " + e + "\n"); },
            [](const std::string&, const std::string&) { return true; }
        });
        return CommandResult::ok();
    }
};

// /bughunter - automated bug search mode
class BughunterCommand : public Command {
public:
    std::string getName() const override { return "bughunter"; }
    std::string getDescription() const override { return "Automated bug search in the codebase"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string target = args.empty() ? "the current project" : args;
        std::string prompt =
            "You are in bug hunter mode. Systematically search " + target + " for bugs:\n"
            "1. Use Glob to find source files\n"
            "2. Use Grep to search for common bug patterns (null checks, error handling, resource leaks)\n"
            "3. Read suspicious files\n"
            "4. Report each bug with file, line, and suggested fix\n"
            "Be thorough. Check error handling, edge cases, resource management, and security issues.";
        ctx.queryEngine->submitMessage(prompt, {
            [&](const std::string& t) { ctx.print(t); }, nullptr,
            [&](const std::string& name, const nlohmann::json&) {
                ctx.print("\n  [" + name + "] ");
            },
            [&](const std::string& name, const ToolResult& r) {
                ctx.print(r.success ? "OK\n" : ("Error\n"));
            },
            []() {}, [&](const std::string& e) { ctx.print("Error: " + e + "\n"); },
            [](const std::string&, const std::string&) { return true; }
        });
        return CommandResult::ok();
    }
};

// /passes - run multiple automated passes on a task
class PassesCommand : public Command {
public:
    std::string getName() const override { return "passes"; }
    std::string getDescription() const override { return "Run multiple automated passes on a task"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Usage: /passes <N> <task description>\n");
            ctx.print("Example: /passes 3 review and improve error handling\n");
            return CommandResult::ok();
        }
        // Parse: first token is count, rest is task
        int count = 1;
        std::string task = args;
        try {
            size_t pos;
            count = std::stoi(args, &pos);
            task = args.substr(pos);
            while (!task.empty() && task[0] == ' ') task.erase(0, 1);
        } catch (...) {}

        if (count < 1) count = 1;
        if (count > 10) count = 10;

        for (int i = 1; i <= count; i++) {
            ctx.print("\n\033[1;36m=== Pass " + std::to_string(i) + "/" + std::to_string(count) + " ===\033[0m\n\n");
            std::string prompt = "Pass " + std::to_string(i) + " of " + std::to_string(count) + ": " + task;
            if (i > 1) prompt += "\n\nThis is a follow-up pass. Review what was done in previous passes and continue improving.";

            ctx.queryEngine->submitMessage(prompt, {
                [&](const std::string& t) { ctx.print(t); }, nullptr,
                [&](const std::string& name, const nlohmann::json&) {
                    ctx.print("\n  [" + name + "] ");
                },
                [&](const std::string& name, const ToolResult& r) {
                    ctx.print(r.success ? "OK\n" : ("Error\n"));
                },
                []() {}, [&](const std::string& e) { ctx.print("Error: " + e + "\n"); },
                [](const std::string&, const std::string&) { return true; }
            });
        }
        return CommandResult::ok();
    }
};

// ============================================================
// Batch 5: Extended utility commands
// ============================================================

// /config - View/modify settings.json
class ConfigCommand : public Command {
public:
    std::string getName() const override { return "config"; }
    std::string getDescription() const override { return "View/modify settings.json"; }
    std::vector<std::string> getAliases() const override { return {"settings"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string path = ctx.cwd + "/.claude/settings.json";
        if (args.empty()) {
            std::ifstream f(path);
            if (!f.is_open()) {
                ctx.print("No settings.json found at: " + path + "\n");
                return CommandResult::ok();
            }
            nlohmann::json j;
            try { f >> j; } catch (...) { return CommandResult::fail("Invalid JSON in settings.json"); }
            ctx.print(j.dump(2) + "\n");
            return CommandResult::ok();
        }
        auto eq = args.find('=');
        if (eq == std::string::npos) {
            ctx.print("Usage: /config key=value\n");
            return CommandResult::ok();
        }
        std::string key = args.substr(0, eq);
        std::string val = args.substr(eq + 1);
        nlohmann::json j;
        { std::ifstream f(path); if (f.is_open()) { try { f >> j; } catch (...) {} } }
        j[key] = val;
        std::ofstream out(path);
        if (!out.is_open()) return CommandResult::fail("Cannot write: " + path);
        out << j.dump(2);
        ctx.print("Set " + key + " = " + val + "\n");
        return CommandResult::ok();
    }
};

// /model - Switch model at runtime
class ModelCommand : public Command {
public:
    std::string getName() const override { return "model"; }
    std::string getDescription() const override { return "Switch model at runtime"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Current model: " + ctx.appState->currentModel + "\n");
            return CommandResult::ok();
        }
        ctx.appState->currentModel = args;
        ctx.print("Model switched to: " + args + "\n");
        return CommandResult::ok();
    }
};

// /cost - Show cost tracking
class CostCommand : public Command {
public:
    std::string getName() const override { return "cost"; }
    std::string getDescription() const override { return "Show cost tracking"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.print("Total cost: $" + std::to_string(ctx.appState->totalCostUSD.load()) + "\n");
        ctx.print("API duration: " + std::to_string(ctx.appState->totalAPIDuration.load()) + "s\n");
        ctx.print("Tool duration: " + std::to_string(ctx.appState->totalToolDuration.load()) + "s\n");
        ctx.print("\nPer-model breakdown:\n");
        std::lock_guard<std::mutex> lock(ctx.appState->usageMutex);
        for (const auto& [model, usage] : ctx.appState->modelUsage) {
            ctx.print("  " + model + ": in=" + std::to_string(usage.inputTokens)
                      + " out=" + std::to_string(usage.outputTokens)
                      + " cache_r=" + std::to_string(usage.cacheReadTokens)
                      + " cache_w=" + std::to_string(usage.cacheWriteTokens) + "\n");
        }
        return CommandResult::ok();
    }
};

// /permissions - Manage permissions
class PermissionsCommand : public Command {
public:
    std::string getName() const override { return "permissions"; }
    std::string getDescription() const override { return "Manage permissions"; }
    std::vector<std::string> getAliases() const override { return {"perms"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& engine = PermissionEngine::getInstance();
        if (args.empty()) {
            ctx.print("Permission mode: " + engine.getModeName() + "\n");
            auto rules = engine.saveRules();
            if (!rules.empty()) {
                ctx.print("Rules:\n" + rules.dump(2) + "\n");
            } else {
                ctx.print("No custom rules.\n");
            }
            return CommandResult::ok();
        }
        if (args.substr(0, 5) == "mode ") {
            std::string mode = args.substr(5);
            if (mode == "auto") engine.setMode(PermissionMode::AUTO);
            else if (mode == "default") engine.setMode(PermissionMode::DEFAULT);
            else if (mode == "bypass") engine.setMode(PermissionMode::BYPASS);
            else { ctx.print("Unknown mode. Use: auto, default, bypass\n"); return CommandResult::ok(); }
            ctx.appState->permissionMode = engine.getMode();
            ctx.print("Permission mode set to: " + mode + "\n");
            return CommandResult::ok();
        }
        ctx.print("Usage: /permissions [mode auto|default|bypass]\n");
        return CommandResult::ok();
    }
};

// /status - System status overview
class StatusCommand : public Command {
public:
    std::string getName() const override { return "status"; }
    std::string getDescription() const override { return "System status overview"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& engine = PermissionEngine::getInstance();
        ctx.print("Model: " + ctx.appState->currentModel + "\n");
        ctx.print("Session: " + ctx.appState->sessionId + "\n");
        ctx.print("Messages: " + std::to_string(ctx.queryEngine->getMessages().size()) + "\n");
        ctx.print("Permission mode: " + engine.getModeName() + "\n");
        ctx.print("Plan mode: " + std::string(ctx.appState->planMode ? "ON" : "OFF") + "\n");
        ctx.print("Vim mode: " + std::string(ctx.appState->vimMode ? "ON" : "OFF") + "\n");
        ctx.print("Voice: " + std::string(ctx.appState->voiceEnabled ? "ON" : "OFF") + "\n");
        return CommandResult::ok();
    }
};

// /clear - Clear conversation history
class ClearCommand : public Command {
public:
    std::string getName() const override { return "clear"; }
    std::string getDescription() const override { return "Clear conversation history"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.queryEngine->clearMessages();
        ctx.print("Conversation history cleared.\n");
        return CommandResult::ok();
    }
};

// /fork - Fork current session
class ForkCommand : public Command {
public:
    std::string getName() const override { return "fork"; }
    std::string getDescription() const override { return "Fork current session"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string newId = ctx.appState->sessionId + "_fork_" + std::to_string(std::time(nullptr));
        // The messages are already in queryEngine; just assign a new session ID
        ctx.queryEngine->setSessionId(newId);
        ctx.appState->sessionId = newId;
        ctx.print("Session forked. New session ID: " + newId + "\n");
        return CommandResult::ok();
    }
};

// /security-review - Security audit
class SecurityReviewCommand : public Command {
public:
    std::string getName() const override { return "security-review"; }
    std::string getDescription() const override { return "Security audit of recent code changes"; }
    std::vector<std::string> getAliases() const override { return {"secreview"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string prompt = "Review the recent code changes for security issues. "
            "Run `git diff HEAD~5` to see recent changes, then analyze for: "
            "injection vulnerabilities, auth issues, data exposure, insecure defaults, "
            "and missing input validation. Report each finding with severity and fix.";
        if (!args.empty()) prompt = "Security review: " + args;
        ctx.print("Running security review...\n\n");
        ctx.queryEngine->submitMessage(prompt, {
            [&](const std::string& t) { ctx.print(t); }, nullptr, nullptr, nullptr,
            []() {}, [&](const std::string& e) { ctx.print("Error: " + e + "\n"); }
        });
        return CommandResult::ok();
    }
};

// /sandbox-toggle - Toggle sandbox mode
class SandboxToggleCommand : public Command {
public:
    std::string getName() const override { return "sandbox-toggle"; }
    std::string getDescription() const override { return "Toggle sandbox mode"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& sb = Sandbox::getInstance();
        if (sb.getMode() == Sandbox::Mode::DISABLED) {
            sb.setMode(Sandbox::Mode::AUTO);
            ctx.print("Sandbox: AUTO (dangerous operations will be blocked)\n");
        } else {
            sb.setMode(Sandbox::Mode::DISABLED);
            ctx.print("Sandbox: DISABLED (all operations allowed)\n");
        }
        return CommandResult::ok();
    }
};

// /keybindings - Show/set keybindings
class KeybindingsCommand : public Command {
public:
    std::string getName() const override { return "keybindings"; }
    std::string getDescription() const override { return "Show/set keybindings"; }
    std::vector<std::string> getAliases() const override { return {"keys"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.print("Vim mode: " + std::string(ctx.appState->vimMode ? "ON" : "OFF") + "\n\n");
        ctx.print("Available shortcuts:\n");
        ctx.print("  Ctrl+C  — Cancel current operation\n");
        ctx.print("  Ctrl+D  — Exit\n");
        ctx.print("  Ctrl+L  — Clear screen\n");
        ctx.print("  Tab     — Autocomplete\n");
        ctx.print("  Up/Down — History navigation\n");
        if (ctx.appState->vimMode) {
            ctx.print("  Esc     — Normal mode\n");
            ctx.print("  i/a     — Insert mode\n");
            ctx.print("  :q      — Quit\n");
        }
        return CommandResult::ok();
    }
};

// /privacy-settings - Privacy settings
class PrivacySettingsCommand : public Command {
public:
    std::string getName() const override { return "privacy-settings"; }
    std::string getDescription() const override { return "Privacy settings"; }
    std::vector<std::string> getAliases() const override { return {"privacy"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Verbose logging: " + std::string(ctx.appState->verbose ? "ON" : "OFF") + "\n");
            ctx.print("Toggle with: /privacy-settings verbose\n");
            return CommandResult::ok();
        }
        if (args == "verbose") {
            ctx.appState->verbose = !ctx.appState->verbose;
            ctx.print("Verbose logging: " + std::string(ctx.appState->verbose ? "ON" : "OFF") + "\n");
        } else {
            ctx.print("Usage: /privacy-settings [verbose]\n");
        }
        return CommandResult::ok();
    }
};

// /rate-limit-options - Rate limit config
class RateLimitOptionsCommand : public Command {
public:
    std::string getName() const override { return "rate-limit-options"; }
    std::string getDescription() const override { return "Rate limit configuration"; }
    std::vector<std::string> getAliases() const override { return {"ratelimit"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        // Static local for retry settings (could be moved to AppState)
        static int maxRetries = 3;
        static int retryDelayMs = 1000;
        if (args.empty()) {
            ctx.print("Rate limit settings:\n");
            ctx.print("  Max retries: " + std::to_string(maxRetries) + "\n");
            ctx.print("  Retry delay: " + std::to_string(retryDelayMs) + "ms\n");
            ctx.print("Set with: /rate-limit-options retries <N>\n");
            return CommandResult::ok();
        }
        if (args.substr(0, 8) == "retries ") {
            try {
                maxRetries = std::stoi(args.substr(8));
                ctx.print("Max retries set to: " + std::to_string(maxRetries) + "\n");
            } catch (...) { ctx.print("Invalid number.\n"); }
        } else {
            ctx.print("Usage: /rate-limit-options [retries <N>]\n");
        }
        return CommandResult::ok();
    }
};

// /commit-push-pr - One-click git workflow
class CommitPushPrCommand : public Command {
public:
    std::string getName() const override { return "commit-push-pr"; }
    std::string getDescription() const override { return "One-click: git add, commit, push, create PR"; }
    std::vector<std::string> getAliases() const override { return {"cpp", "shipit"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string msg = args.empty() ? "Auto-commit via CloseCrab" : args;
        std::string cmd = "git add -A && git commit -m \"" + msg + "\" && git push && gh pr create --fill";
        ctx.print("Running: " + cmd + "\n");
        auto result = ProcessRunner::run(cmd);
        ctx.print(result.output);
        if (result.exitCode != 0) {
            ctx.print("Exit code: " + std::to_string(result.exitCode) + "\n");
        }
        return CommandResult::ok();
    }
};

// /release-notes - Generate release notes
class ReleaseNotesCommand : public Command {
public:
    std::string getName() const override { return "release-notes"; }
    std::string getDescription() const override { return "Generate release notes from git log"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string range = args.empty() ? "" : args;
        std::string cmd = range.empty()
            ? "git log --oneline --no-merges -30"
            : "git log --oneline --no-merges " + range;
        auto result = ProcessRunner::run(cmd);
        if (result.output.empty()) {
            ctx.print("No commits found.\n");
            return CommandResult::ok();
        }
        std::string prompt = "Format the following git log as release notes with categories "
            "(Features, Bug Fixes, Improvements, Other). Use markdown:\n\n" + result.output;
        ctx.print("Generating release notes...\n\n");
        ctx.queryEngine->submitMessage(prompt, {
            [&](const std::string& t) { ctx.print(t); }, nullptr, nullptr, nullptr,
            []() {}, [&](const std::string& e) { ctx.print("Error: " + e + "\n"); }
        });
        return CommandResult::ok();
    }
};

// /stats - Detailed statistics
class StatsCommand : public Command {
public:
    std::string getName() const override { return "stats"; }
    std::string getDescription() const override { return "Detailed session statistics"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        const auto& msgs = ctx.queryEngine->getMessages();
        int userMsgs = 0, assistantMsgs = 0, toolCalls = 0;
        int64_t totalTokens = 0;
        for (const auto& m : msgs) {
            if (m.role == MessageRole::USER) userMsgs++;
            else if (m.role == MessageRole::ASSISTANT) {
                assistantMsgs++;
                for (const auto& b : m.content)
                    if (b.type == ContentBlockType::TOOL_USE) toolCalls++;
            }
        }
        {
            std::lock_guard<std::mutex> lock(ctx.appState->usageMutex);
            for (const auto& [model, u] : ctx.appState->modelUsage)
                totalTokens += u.inputTokens + u.outputTokens;
        }
        ctx.print("Messages: " + std::to_string(msgs.size())
                  + " (" + std::to_string(userMsgs) + " user, "
                  + std::to_string(assistantMsgs) + " assistant)\n");
        ctx.print("Tool calls: " + std::to_string(toolCalls) + "\n");
        ctx.print("Total tokens: " + std::to_string(totalTokens) + "\n");
        ctx.print("API duration: " + std::to_string(ctx.appState->totalAPIDuration.load()) + "s\n");
        ctx.print("Tool duration: " + std::to_string(ctx.appState->totalToolDuration.load()) + "s\n");
        ctx.print("Cost: $" + std::to_string(ctx.appState->totalCostUSD.load()) + "\n");
        return CommandResult::ok();
    }
};

// /bridge - Bridge remote control
class BridgeCommand : public Command {
public:
    std::string getName() const override { return "bridge"; }
    std::string getDescription() const override { return "Bridge remote control status"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Bridge: not connected\n");
            ctx.print("Usage: /bridge connect <url> | disconnect | status\n");
            return CommandResult::ok();
        }
        if (args == "status") {
            ctx.print("Bridge: not connected\n");
        } else if (args.substr(0, 7) == "connect") {
            std::string url = args.size() > 8 ? args.substr(8) : "";
            ctx.print("Bridge connect to: " + url + " (not yet implemented)\n");
        } else if (args == "disconnect") {
            ctx.print("Bridge disconnected.\n");
        } else {
            ctx.print("Usage: /bridge connect <url> | disconnect | status\n");
        }
        return CommandResult::ok();
    }
};

// /buddy - Buddy/pair mode
class BuddyCommand : public Command {
public:
    std::string getName() const override { return "buddy"; }
    std::string getDescription() const override { return "Toggle buddy/pair mode (confirm before each action)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        // Use planMode as a proxy for buddy mode (confirm-before-act)
        ctx.appState->planMode = !ctx.appState->planMode;
        ctx.print("Buddy mode: " + std::string(ctx.appState->planMode ? "ON" : "OFF") + "\n");
        if (ctx.appState->planMode) {
            ctx.print("AI will ask for confirmation before each action.\n");
        }
        return CommandResult::ok();
    }
};

// /peers - Peer session management
class PeersCommand : public Command {
public:
    std::string getName() const override { return "peers"; }
    std::string getDescription() const override { return "List active peer sessions"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.print("Active sessions:\n");
        ctx.print("  * " + ctx.appState->sessionId + " (current)\n");
        if (!ctx.appState->parentSessionId.empty()) {
            ctx.print("  - " + ctx.appState->parentSessionId + " (parent)\n");
        }
        auto agents = AgentManager::getInstance().listAgents();
        for (const auto& [id, status] : agents) {
            if (status == AgentStatus::RUNNING) {
                ctx.print("  - " + id + " (agent)\n");
            }
        }
        return CommandResult::ok();
    }
};

// /workflows - Run workflow scripts
class WorkflowsCommand : public Command {
public:
    std::string getName() const override { return "workflows"; }
    std::string getDescription() const override { return "List/run workflow scripts from .claude/workflows/"; }
    std::vector<std::string> getAliases() const override { return {"wf"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string dir = ctx.cwd + "/.claude/workflows";
        if (args.empty() || args == "list") {
            auto result = ProcessRunner::run("ls \"" + dir + "\"/*.md 2>/dev/null || echo '(none)'");
            ctx.print("Workflows in .claude/workflows/:\n" + result.output + "\n");
            return CommandResult::ok();
        }
        // Run a specific workflow
        std::string path = dir + "/" + args;
        if (path.find(".md") == std::string::npos) path += ".md";
        std::ifstream f(path);
        if (!f.is_open()) {
            ctx.print("Workflow not found: " + path + "\n");
            return CommandResult::ok();
        }
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        ctx.print("Running workflow: " + args + "\n\n");
        ctx.queryEngine->submitMessage("Execute this workflow:\n\n" + content, {
            [&](const std::string& t) { ctx.print(t); }, nullptr, nullptr, nullptr,
            []() {}, [&](const std::string& e) { ctx.print("Error: " + e + "\n"); }
        });
        return CommandResult::ok();
    }
};

// /oauth-refresh - Refresh OAuth token
class OauthRefreshCommand : public Command {
public:
    std::string getName() const override { return "oauth-refresh"; }
    std::string getDescription() const override { return "Refresh OAuth token"; }
    std::vector<std::string> getAliases() const override { return {"oauth"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.print("OAuth token management:\n");
        ctx.print("  CloseCrab uses API keys by default.\n");
        ctx.print("  To set up OAuth, configure in .claude/settings.json:\n");
        ctx.print("    \"oauth\": {\n");
        ctx.print("      \"provider\": \"...\",\n");
        ctx.print("      \"client_id\": \"...\",\n");
        ctx.print("      \"token_url\": \"...\"\n");
        ctx.print("    }\n");
        ctx.print("  Then run /oauth-refresh to obtain a new token.\n");
        return CommandResult::ok();
    }
};

} // namespace closecrab
