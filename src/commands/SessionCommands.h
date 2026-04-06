#pragma once

#include "Command.h"
#include "../core/QueryEngine.h"
#include "../memory/MemorySystem.h"
#include "../core/SessionManager.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>

namespace closecrab {

// /session - show or manage sessions
class SessionCommand : public Command {
public:
    std::string getName() const override { return "session"; }
    std::string getDescription() const override { return "Show current session info"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.print("Session ID: " + ctx.queryEngine->getSessionId() + "\n");
        ctx.print("Working dir: " + ctx.cwd + "\n");
        ctx.print("Messages: " + std::to_string(ctx.queryEngine->getMessages().size()) + "\n");
        return CommandResult::ok();
    }
};

// /new - start new session
class NewSessionCommand : public Command {
public:
    std::string getName() const override { return "new"; }
    std::string getDescription() const override { return "Start a new conversation session"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.queryEngine->clearMessages();
        ctx.print("New session started.\n");
        return CommandResult::ok();
    }
};

// /history - show conversation history
class HistoryCommand : public Command {
public:
    std::string getName() const override { return "history"; }
    std::string getDescription() const override { return "Show conversation history"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        const auto& msgs = ctx.queryEngine->getMessages();
        if (msgs.empty()) {
            ctx.print("No messages in current session.\n");
            return CommandResult::ok();
        }

        int count = args.empty() ? 20 : std::stoi(args);
        int start = std::max(0, (int)msgs.size() - count);

        for (int i = start; i < (int)msgs.size(); i++) {
            const auto& msg = msgs[i];
            std::string role = (msg.role == MessageRole::USER) ? "\033[36mUser\033[0m" : "\033[32mAssistant\033[0m";
            std::string text = msg.getText();
            if (text.size() > 200) text = text.substr(0, 200) + "...";
            ctx.print(role + ": " + text + "\n");
        }
        return CommandResult::ok();
    }
};

// /export - export conversation to file
class ExportCommand : public Command {
public:
    std::string getName() const override { return "export"; }
    std::string getDescription() const override { return "Export conversation to a file"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        namespace fs = std::filesystem;
        std::string filename = args;
        if (filename.empty()) {
            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << "session_" << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S") << ".md";
            filename = oss.str();
        }

        std::ofstream f(filename);
        if (!f.is_open()) return CommandResult::fail("Cannot create file: " + filename);

        f << "# CloseCrab Session Export\n\n";
        for (const auto& msg : ctx.queryEngine->getMessages()) {
            std::string role = (msg.role == MessageRole::USER) ? "User" : "Assistant";
            f << "## " << role << "\n\n" << msg.getText() << "\n\n---\n\n";
        }

        ctx.print("Exported to: " + filename + "\n");
        return CommandResult::ok();
    }
};

// /compact - compress conversation history
class CompactCommand : public Command {
public:
    std::string getName() const override { return "compact"; }
    std::string getDescription() const override { return "Compress conversation history to save context"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        const auto& msgs = ctx.queryEngine->getMessages();
        if (msgs.size() < 10) {
            ctx.print("History too short to compact (" + std::to_string(msgs.size()) + " messages).\n");
            return CommandResult::ok();
        }

        // Simple compaction: keep last N messages, summarize the rest
        int keepCount = 10;
        int removeCount = (int)msgs.size() - keepCount;
        ctx.print("Compacting " + std::to_string(removeCount) + " old messages...\n");

        // For now, just truncate. Full implementation would use LLM to summarize.
        auto& mutableMsgs = const_cast<std::vector<Message>&>(ctx.queryEngine->getMessages());
        std::string summary = "Previous conversation had " + std::to_string(removeCount) + " messages (compacted).";

        std::vector<Message> newMsgs;
        newMsgs.push_back(Message::makeSystem(SystemSubtype::COMPACT_BOUNDARY, summary));
        for (int i = removeCount; i < (int)mutableMsgs.size(); i++) {
            newMsgs.push_back(std::move(mutableMsgs[i]));
        }
        mutableMsgs = std::move(newMsgs);

        ctx.print("Compacted. Kept " + std::to_string(keepCount) + " recent messages.\n");
        return CommandResult::ok();
    }
};

// /context - show context info
class ContextCommand : public Command {
public:
    std::string getName() const override { return "context"; }
    std::string getDescription() const override { return "Show current context information"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        const auto& msgs = ctx.queryEngine->getMessages();
        int totalChars = 0;
        for (const auto& m : msgs) totalChars += (int)m.getText().size();

        ctx.print("Messages: " + std::to_string(msgs.size()) + "\n");
        ctx.print("Total chars: " + std::to_string(totalChars) + "\n");
        ctx.print("Est. tokens: ~" + std::to_string(totalChars / 4) + "\n");
        ctx.print("CLAUDE.md: " + std::string(ctx.appState->claudeMdContent.empty() ? "not loaded" : "loaded") + "\n");
        return CommandResult::ok();
    }
};

// /env - show environment info
class EnvCommand : public Command {
public:
    std::string getName() const override { return "env"; }
    std::string getDescription() const override { return "Show environment variables and config"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto getEnv = [](const char* name) -> std::string {
            const char* v = std::getenv(name);
            return v ? v : "(not set)";
        };

        ctx.print(std::string("ANTHROPIC_AUTH_TOKEN: ") + (getEnv("ANTHROPIC_AUTH_TOKEN") != "(not set)" ? "***set***" : "(not set)") + "\n");
        ctx.print("ANTHROPIC_BASE_URL: " + getEnv("ANTHROPIC_BASE_URL") + "\n");
        ctx.print("ANTHROPIC_MODEL: " + getEnv("ANTHROPIC_MODEL") + "\n");
        ctx.print("CLAUDE_LOCAL_PROVIDER: " + getEnv("CLAUDE_LOCAL_PROVIDER") + "\n");
        ctx.print("CWD: " + ctx.cwd + "\n");
        return CommandResult::ok();
    }
};

// /version
class VersionCommand : public Command {
public:
    std::string getName() const override { return "version"; }
    std::string getDescription() const override { return "Show version information"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.print("CloseCrab-Unified v0.1.0\n");
        ctx.print("Model: " + ctx.appState->currentModel + "\n");
        return CommandResult::ok();
    }
};

// /fast - toggle fast mode
class FastCommand : public Command {
public:
    std::string getName() const override { return "fast"; }
    std::string getDescription() const override { return "Toggle fast mode"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.appState->fastMode = !ctx.appState->fastMode;
        ctx.print("Fast mode: " + std::string(ctx.appState->fastMode ? "ON" : "OFF") + "\n");
        return CommandResult::ok();
    }
};

// /thinking - toggle thinking mode
class ThinkingCommand : public Command {
public:
    std::string getName() const override { return "thinking"; }
    std::string getDescription() const override { return "Toggle extended thinking mode"; }
    std::vector<std::string> getAliases() const override { return {"think", "effort"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.appState->thinkingConfig.enabled = !ctx.appState->thinkingConfig.enabled;
        } else if (args == "on") {
            ctx.appState->thinkingConfig.enabled = true;
        } else if (args == "off") {
            ctx.appState->thinkingConfig.enabled = false;
        } else {
            // Set budget
            try {
                ctx.appState->thinkingConfig.budgetTokens = std::stoi(args);
                ctx.appState->thinkingConfig.enabled = true;
            } catch (...) {
                return CommandResult::fail("Usage: /thinking [on|off|<budget_tokens>]");
            }
        }
        ctx.print("Thinking: " + std::string(ctx.appState->thinkingConfig.enabled ? "ON" : "OFF") +
                  " (budget: " + std::to_string(ctx.appState->thinkingConfig.budgetTokens) + " tokens)\n");
        return CommandResult::ok();
    }
};

} // namespace closecrab
