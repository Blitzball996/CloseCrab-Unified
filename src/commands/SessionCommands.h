#pragma once

#include "Command.h"
#include "../core/QueryEngine.h"
#include "../memory/MemorySystem.h"
#include "../memory/SessionSearch.h"
#include "../core/SessionManager.h"
#include "../core/TranscriptStore.h"
#include "../services/CompactSummary.h"
#include <filesystem>
#include <fstream>
#include <chrono>

// Version injected by CMake (target_compile_definitions). Fallback for non-CMake builds.
#ifndef CLOSECRAB_VERSION
#define CLOSECRAB_VERSION "dev"
#endif
#include <iomanip>
#include <iostream>
#include <sstream>

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

// /resume - list and restore past sessions
class ResumeCommand : public Command {
public:
    std::string getName() const override { return "resume"; }
    std::string getDescription() const override { return "List recent sessions and restore one"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        // If a specific fork/session ID is passed directly, try to restore it.
        if (!args.empty() && args.find_first_not_of("0123456789") != std::string::npos) {
            namespace fs = std::filesystem;
            std::string forkPath = "data/forks/" + args + ".json";
            if (fs::exists(forkPath)) {
                return restoreFork(forkPath, args, ctx);
            }
            return restoreSessionId(args, ctx);
        }

        // List recent sessions from the JSONL transcripts (JackProAi model).
        auto sessions = TranscriptStore::list(10);
        if (sessions.empty()) {
            ctx.print("No saved sessions found.\n");
            return CommandResult::ok();
        }

        ctx.print("\n\033[1mRecent sessions:\033[0m\n");
        for (size_t i = 0; i < sessions.size(); i++) {
            auto& s = sessions[i];
            std::string timestamp = formatTimestamp(s.mtime);
            std::string preview = s.firstUserPreview.empty() ? "(session data)" : s.firstUserPreview;
            ctx.print("  \033[36m" + std::to_string(i + 1) + ".\033[0m [" + timestamp + "] ");
            ctx.print("\"" + preview + "\"");
            ctx.print(" (" + std::to_string(s.messageCount) + " messages)\n");
        }
        ctx.print("\nEnter number to restore (or press Enter for most recent): ");

        std::string input;
        std::getline(std::cin, input);

        int choice = 0;
        if (!input.empty()) {
            try {
                choice = std::stoi(input) - 1;
            } catch (...) {
                ctx.print("Invalid selection.\n");
                return CommandResult::ok();
            }
        }
        if (choice < 0 || choice >= (int)sessions.size()) {
            ctx.print("Invalid selection.\n");
            return CommandResult::ok();
        }

        return restoreSessionId(sessions[choice].sessionId, ctx);
    }

private:
    CommandResult restoreFork(const std::string& forkPath, const std::string& forkId, CommandContext& ctx) {
        try {
            std::ifstream f(forkPath);
            if (!f.is_open()) return CommandResult::fail("Cannot open fork file: " + forkPath);
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            auto data = nlohmann::json::parse(content);
            ctx.queryEngine->deserializeMessages(data);
            ctx.print("Restored " + std::to_string(ctx.queryEngine->getMessages().size()) +
                      " messages from fork [" + forkId + "]\n");
        } catch (const std::exception& e) {
            ctx.print("Failed to restore fork: " + std::string(e.what()) + "\n");
        }
        return CommandResult::ok();
    }

    // Restore from the JSONL transcript (JackProAi sessionStorage model).
    // Falls back to the legacy SQLite context blob for sessions saved before
    // the transcript switch.
    CommandResult restoreSessionId(const std::string& sessionId, CommandContext& ctx) {
        auto msgs = TranscriptStore::load(sessionId);
        if (!msgs.empty()) {
            // Serialize to the deserializeMessages shape and hand off.
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& m : msgs) arr.push_back(TranscriptStore::messageToEntry(m));
            ctx.queryEngine->deserializeMessages(arr);
            ctx.print("Restored " + std::to_string(ctx.queryEngine->getMessages().size()) +
                      " messages from session [" + sessionId + "]\n");
            return CommandResult::ok();
        }
        // Legacy fallback: SQLite context blob.
        SessionManager mgr("data/closecrab.db");
        return restoreSession(sessionId, mgr, ctx);
    }

    CommandResult restoreSession(const std::string& sessionId, SessionManager& mgr, CommandContext& ctx) {
        auto session = mgr.getSession(sessionId);
        if (!session || session->context.empty() || session->context == "{}") {
            ctx.print("No saved session data found for: " + sessionId + "\n");
            return CommandResult::ok();
        }

        try {
            auto data = nlohmann::json::parse(session->context);
            ctx.queryEngine->deserializeMessages(data);
            ctx.print("Restored " + std::to_string(ctx.queryEngine->getMessages().size()) +
                      " messages from session [" + sessionId + "]\n");
        } catch (const std::exception& e) {
            ctx.print("Failed to restore session: " + std::string(e.what()) + "\n");
        }
        return CommandResult::ok();
    }

    std::string formatTimestamp(long long ts) {
        std::time_t t = static_cast<std::time_t>(ts);
        std::tm* tm = std::localtime(&t);
        if (!tm) return "unknown";
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d %H:%M");
        return oss.str();
    }

    std::string extractPreview(const std::string& context) {
        if (context.empty() || context == "{}") return "(empty)";
        try {
            auto data = nlohmann::json::parse(context);
            // Stored format is a bare ARRAY of {role, text, content:[...]}
            // (QueryEngine::serializeMessages). Find the first user message.
            const nlohmann::json* arr = nullptr;
            if (data.is_array()) arr = &data;
            else if (data.contains("messages") && data["messages"].is_array()) arr = &data["messages"];
            if (arr) {
                for (const auto& msg : *arr) {
                    if (msg.value("role", "") != "user") continue;
                    std::string text = msgText(msg);
                    if (text.empty()) continue;
                    if (text.size() > 50) text = text.substr(0, 50) + "...";
                    return text;
                }
            }
        } catch (...) {}
        return "(session data)";
    }

    int countMessages(const std::string& context) {
        if (context.empty() || context == "{}") return 0;
        try {
            auto data = nlohmann::json::parse(context);
            if (data.is_array()) return (int)data.size();
            if (data.contains("messages") && data["messages"].is_array())
                return (int)data["messages"].size();
        } catch (...) {}
        return 0;
    }

    // Extract display text from a stored message in either shape:
    // {text:"..."} (serializeMessages) or {content:[{text:"..."}]} (toApiJson).
    static std::string msgText(const nlohmann::json& msg) {
        if (msg.contains("text") && msg["text"].is_string())
            return msg["text"].get<std::string>();
        if (msg.contains("content")) {
            if (msg["content"].is_string()) return msg["content"].get<std::string>();
            if (msg["content"].is_array()) {
                for (const auto& b : msg["content"]) {
                    if (b.contains("text") && b["text"].is_string())
                        return b["text"].get<std::string>();
                }
            }
        }
        return "";
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
        if (msgs.size() < 6) {
            ctx.print("History too short to compact (" + std::to_string(msgs.size()) + " messages).\n");
            return CommandResult::ok();
        }

        size_t before = msgs.size();

        // Show summary of what will be compacted (keep last ~40% of messages)
        int keepCount = std::max(4, (int)(msgs.size() * 2 / 5));
        int compactEnd = (int)msgs.size() - keepCount - 1;
        if (compactEnd > 0) {
            std::string summary = CompactSummary::summarize(msgs, 0, compactEnd);
            ctx.print("\033[2m" + summary + "\033[0m\n");
        }

        bool compacted = ctx.queryEngine->compactHistory();
        if (compacted) {
            size_t after = ctx.queryEngine->getMessages().size();
            ctx.print("Compacted: " + std::to_string(before) + " -> " + std::to_string(after) + " messages.\n");
        } else {
            ctx.print("Nothing to compact.\n");
        }
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
        ctx.print("CloseCrab-Unified v" CLOSECRAB_VERSION "\n");
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

// /search - search conversation history
class SearchCommand : public Command {
public:
    std::string getName() const override { return "search"; }
    std::string getDescription() const override { return "Search conversation history by keyword"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Usage: /search <keyword>\n");
            return CommandResult::ok();
        }
        auto results = SessionSearch::search("data/closecrab.db", args);
        if (results.empty()) {
            ctx.print("No results found for: " + args + "\n");
            return CommandResult::ok();
        }
        ctx.print("Found " + std::to_string(results.size()) + " sessions matching \"" + args + "\":\n\n");
        for (size_t i = 0; i < results.size(); i++) {
            ctx.print("  " + std::to_string(i+1) + ". [" + results[i].sessionId + "] "
                + "(" + std::to_string(results[i].relevanceScore) + " matches)\n"
                + "     " + results[i].matchedContent + "\n\n");
        }
        return CommandResult::ok();
    }
};

} // namespace closecrab
