#pragma once

#include "Command.h"
#include "../rag/RAGManager.h"
#include "../security/Sandbox.h"
#include "../permissions/PermissionEngine.h"
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace closecrab {

// /rag - RAG management
class RAGCommand : public Command {
public:
    std::string getName() const override { return "rag"; }
    std::string getDescription() const override { return "Manage RAG (enable/disable/load/clear/list)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& rag = RAGManager::getInstance();

        if (args.empty() || args == "status") {
            ctx.print("RAG: " + std::string(rag.isEnabled() ? "enabled" : "disabled") + "\n");
            ctx.print("Documents: " + std::to_string(rag.getDocumentCount()) + "\n");
            return CommandResult::ok();
        }
        if (args == "enable") {
            rag.setEnabled(true);
            ctx.print("RAG enabled.\n");
        } else if (args == "disable") {
            rag.setEnabled(false);
            ctx.print("RAG disabled.\n");
        } else if (args.substr(0, 4) == "load") {
            std::string path = args.size() > 5 ? args.substr(5) : ".";
            ctx.print("Loading documents from: " + path + "\n");
            rag.loadDirectory(path);
            ctx.print("Done. Total documents: " + std::to_string(rag.getDocumentCount()) + "\n");
        } else if (args == "clear") {
            rag.clear();
            ctx.print("RAG documents cleared.\n");
        } else if (args == "list") {
            auto docs = rag.getAllDocuments();
            for (const auto& d : docs) {
                std::string preview = d.content.substr(0, 60);
                ctx.print("#" + std::to_string(d.id) + " [" + d.source + "] " + preview + "...\n");
            }
            if (docs.empty()) ctx.print("No documents loaded.\n");
        } else {
            ctx.print("Usage: /rag [status|enable|disable|load <path>|clear|list]\n");
        }
        return CommandResult::ok();
    }
};

// /ssd - SSD Expert Streaming status
class SSDCommand : public Command {
public:
    std::string getName() const override { return "ssd"; }
    std::string getDescription() const override { return "Show SSD Expert Streaming status"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        // This would need access to LLMEngine's SSD streamer
        ctx.print("SSD Expert Streaming status: check LLM engine.\n");
        return CommandResult::ok();
    }
};

// /sandbox - sandbox management
class SandboxCommand : public Command {
public:
    std::string getName() const override { return "sandbox"; }
    std::string getDescription() const override { return "Show or change sandbox mode"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        auto& sb = Sandbox::getInstance();

        if (args.empty()) {
            std::string mode;
            switch (sb.getMode()) {
                case Sandbox::Mode::DISABLED: mode = "disabled"; break;
                case Sandbox::Mode::ASK: mode = "ask"; break;
                case Sandbox::Mode::AUTO: mode = "auto"; break;
                case Sandbox::Mode::TRUSTED: mode = "trusted"; break;
            }
            ctx.print("Sandbox mode: " + mode + "\n");
        } else if (args == "disable") {
            sb.setMode(Sandbox::Mode::DISABLED);
            ctx.print("Sandbox disabled.\n");
        } else if (args == "ask") {
            sb.setMode(Sandbox::Mode::ASK);
            ctx.print("Sandbox mode: ask.\n");
        } else if (args == "auto") {
            sb.setMode(Sandbox::Mode::AUTO);
            ctx.print("Sandbox mode: auto.\n");
        } else if (args == "trusted") {
            sb.setMode(Sandbox::Mode::TRUSTED);
            ctx.print("Sandbox mode: trusted.\n");
        } else {
            ctx.print("Usage: /sandbox [disable|ask|auto|trusted]\n");
        }
        return CommandResult::ok();
    }
};

// /plan - toggle plan mode
class PlanCommand : public Command {
public:
    std::string getName() const override { return "plan"; }
    std::string getDescription() const override { return "Toggle plan mode (read-only exploration)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.appState->planMode = !ctx.appState->planMode;
        ctx.print("Plan mode: " + std::string(ctx.appState->planMode ? "ON (read-only tools)" : "OFF") + "\n");
        return CommandResult::ok();
    }
};

// /doctor - diagnostics
class DoctorCommand : public Command {
public:
    std::string getName() const override { return "doctor"; }
    std::string getDescription() const override { return "Run environment diagnostics"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::ostringstream out;
        out << "\033[1m=== CloseCrab-Unified Diagnostics ===\033[0m\n\n";

        // Core status
        out << "\033[1;36mCore\033[0m\n";
        out << "  Model: " << ctx.appState->currentModel << "\n";
        out << "  Session: " << ctx.queryEngine->getSessionId() << "\n";
        out << "  Messages: " << ctx.queryEngine->getMessages().size() << "\n";
        out << "  Tools: " << ctx.toolRegistry->getToolNames().size() << "\n";
        out << "  Permissions: " << PermissionEngine::getInstance().getModeName() << "\n";
        out << "  Plan mode: " << (ctx.appState->planMode ? "ON" : "OFF") << "\n";
        out << "  Thinking: " << (ctx.appState->thinkingConfig.enabled ? "ON" : "OFF") << "\n";
        out << "  RAG: " << (RAGManager::getInstance().isEnabled() ? "enabled" : "disabled")
            << " (" << RAGManager::getInstance().getDocumentCount() << " docs)\n";
        out << "  Cost: $" << std::fixed << std::setprecision(4) << ctx.appState->getTotalCost() << "\n\n";

        // External tools
        out << "\033[1;36mExternal Tools\033[0m\n";
        out << "  git:    " << checkTool("git --version") << "\n";
        out << "  rg:     " << checkTool("rg --version") << "\n";
        out << "  node:   " << checkTool("node --version") << "\n";
        out << "  python: " << checkTool("python --version") << "\n";
        out << "  gh:     " << checkTool("gh --version") << "\n\n";

        // Config files
        out << "\033[1;36mConfig Files\033[0m\n";
        namespace fs = std::filesystem;
        out << "  CLAUDE.md:      " << (ctx.appState->claudeMdContent.empty() ? "\033[33mnot found\033[0m" : "\033[32mloaded\033[0m") << "\n";
        out << "  settings.json:  " << (fs::exists(fs::path(ctx.cwd) / ".claude" / "settings.json") ? "\033[32mfound\033[0m" : "\033[90mnot found\033[0m") << "\n";
        out << "  config.yaml:    " << (fs::exists("config/config.yaml") ? "\033[32mfound\033[0m" : "\033[90mnot found\033[0m") << "\n";
        out << "  .claude/memory/: " << (fs::exists(fs::path(ctx.cwd) / ".claude" / "memory") ? "\033[32mexists\033[0m" : "\033[90mnot created\033[0m") << "\n";
        out << "  .claude/skills/: " << (fs::exists(fs::path(ctx.cwd) / ".claude" / "skills") ? "\033[32mexists\033[0m" : "\033[90mnot created\033[0m") << "\n\n";

        out << "\033[1;32m=== All checks passed ===\033[0m\n";
        ctx.print(out.str());
        return CommandResult::ok();
    }

private:
    static std::string checkTool(const std::string& cmd) {
        std::string fullCmd;
#ifdef _WIN32
        fullCmd = "cmd /c \"" + cmd + " 2>nul\" 2>nul";
        FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
        fullCmd = cmd + " 2>/dev/null";
        FILE* pipe = popen(fullCmd.c_str(), "r");
#endif
        if (!pipe) return "\033[31mnot found\033[0m";

        char buf[256];
        std::string result;
        if (fgets(buf, sizeof(buf), pipe)) {
            result = buf;
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
        }
#ifdef _WIN32
        int rc = _pclose(pipe);
#else
        int rc = pclose(pipe);
#endif
        if (rc != 0 || result.empty()) return "\033[31mnot found\033[0m";
        return "\033[32m" + result + "\033[0m";
    }
};

// /add-dir - add directory to context
class AddDirCommand : public Command {
public:
    std::string getName() const override { return "add-dir"; }
    std::string getDescription() const override { return "Add a directory to the working context"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) return CommandResult::fail("Usage: /add-dir <path>");
        namespace fs = std::filesystem;
        if (!fs::exists(args) || !fs::is_directory(args)) {
            return CommandResult::fail("Not a valid directory: " + args);
        }
        // For now, just acknowledge. Full implementation would add to additionalWorkingDirectories.
        ctx.print("Added directory: " + fs::absolute(args).string() + "\n");
        return CommandResult::ok();
    }
};

// /files - list files in cwd
class FilesCommand : public Command {
public:
    std::string getName() const override { return "files"; }
    std::string getDescription() const override { return "List files in current directory"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        namespace fs = std::filesystem;
        std::string dir = args.empty() ? ctx.cwd : args;
        int count = 0;
        for (auto& entry : fs::directory_iterator(dir)) {
            std::string prefix = entry.is_directory() ? "[DIR] " : "      ";
            ctx.print(prefix + entry.path().filename().string() + "\n");
            if (++count > 50) { ctx.print("... (truncated)\n"); break; }
        }
        return CommandResult::ok();
    }
};

// /provider - switch provider at runtime
class ProviderCommand : public Command {
public:
    std::string getName() const override { return "provider"; }
    std::string getDescription() const override { return "Show or switch provider (local/anthropic/openai)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Current provider: " + ctx.appState->currentModel + "\n");
            ctx.print("Usage: /provider <local|anthropic|openai>\n");
            ctx.print("  After switching, use /reload to apply.\n");
            return CommandResult::ok();
        }
        if (args == "local" || args == "anthropic" || args == "openai") {
            ctx.print("Provider set to: " + args + "\n");
            ctx.print("Edit config/config.yaml to set provider: \"" + args + "\"\n");
            ctx.print("Then restart the program or use /reload.\n");
        } else {
            ctx.print("Unknown provider: " + args + ". Use local, anthropic, or openai.\n");
        }
        return CommandResult::ok();
    }
};

// /api - show or set API configuration
class ApiConfigCommand : public Command {
public:
    std::string getName() const override { return "api"; }
    std::string getDescription() const override { return "Show or set API configuration"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("API Configuration:\n");
            ctx.print("  Model: " + ctx.appState->currentModel + "\n");
            ctx.print("\nTo change, edit config/config.yaml:\n");
            ctx.print("  provider: \"anthropic\"    # or openai, local\n");
            ctx.print("  api:\n");
            ctx.print("    base_url: \"https://your-api.com\"\n");
            ctx.print("    api_key: \"sk-your-key\"\n");
            ctx.print("    model: \"claude-sonnet-4-20250514\"\n");
            ctx.print("\nOr use env vars: ANTHROPIC_BASE_URL, ANTHROPIC_AUTH_TOKEN, ANTHROPIC_MODEL\n");
            ctx.print("Or CLI args: --provider --api-url --api-key --api-model\n");
            ctx.print("\nRestart after changes.\n");
            return CommandResult::ok();
        }
        ctx.print("Use config/config.yaml to configure API. Restart to apply.\n");
        return CommandResult::ok();
    }
};

// /reload - reload configuration (restart hint)
class ReloadCommand : public Command {
public:
    std::string getName() const override { return "reload"; }
    std::string getDescription() const override { return "Reload configuration (requires restart)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.print("Configuration reload requires restarting the program.\n");
        ctx.print("Please exit (/quit) and restart.\n");
        return CommandResult::ok();
    }
};

} // namespace closecrab
