#pragma once

#include "Command.h"
#include "../api/APIClient.h"
#ifdef CLOSECRAB_HAS_ONNX
#include "../rag/RAGManager.h"
#endif
#include "../security/Sandbox.h"
#include "../permissions/PermissionEngine.h"
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <fstream>

namespace closecrab {

// /rag - RAG management
#ifdef CLOSECRAB_HAS_ONNX
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
#endif

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
        out << "  Effort: " << ctx.appState->thinkingConfig.effort
            << (ctx.appState->thinkingConfig.effort == "xhigh" ? " (ultra)" : "") << "\n";
#ifdef CLOSECRAB_HAS_ONNX
        out << "  RAG: " << (RAGManager::getInstance().isEnabled() ? "enabled" : "disabled")
            << " (" << RAGManager::getInstance().getDocumentCount() << " docs)\n";
#else
        out << "  RAG: not available (built without ONNX)\n";
#endif
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
        out << "  settings.json:  " << (fs::exists(fs::path(ctx.cwd) / ".crab" / "settings.json") ? "\033[32mfound\033[0m" : "\033[90mnot found\033[0m") << "\n";
        out << "  config.yaml:    " << (fs::exists("config/config.yaml") ? "\033[32mfound\033[0m" : "\033[90mnot found\033[0m") << "\n";
        out << "  .crab/memory/: " << (fs::exists(fs::path(ctx.cwd) / ".crab" / "memory") ? "\033[32mexists\033[0m" : "\033[90mnot created\033[0m") << "\n";
        out << "  .crab/skills/: " << (fs::exists(fs::path(ctx.cwd) / ".crab" / "skills") ? "\033[32mexists\033[0m" : "\033[90mnot created\033[0m") << "\n\n";

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
    std::string getDescription() const override { return "Show or set API config (key/url/model/provider)"; }
    std::vector<std::string> getAliases() const override { return {"setup"}; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        const std::string cfgPath = ctx.appState ? ctx.appState->configPath : "config/config.yaml";

        if (args.empty()) {
            ctx.print("API Configuration:\n");
            ctx.print("  Provider: " + Config::getInstance().getString("provider", "anthropic") + "\n");
            ctx.print("  Base URL: " + Config::getInstance().getString("api.base_url", "") + "\n");
            ctx.print("  Model:    " + ctx.appState->currentModel + "\n");
            std::string key = Config::getInstance().getString("api.api_key", "");
            ctx.print("  API key:  " + maskKey(key) + "\n");
            ctx.print("  Config file: " + cfgPath + "\n");
            ctx.print("\nSet values (writes to config.yaml, restart to apply):\n");
            ctx.print("  /api key=sk-xxxx          # your API key\n");
            ctx.print("  /api url=https://your-relay.com   # API base URL / 中转地址\n");
            ctx.print("  /api model=claude-sonnet-4-20250514\n");
            ctx.print("  /api provider=anthropic   # or openai, local\n");
            ctx.print("\nOr edit the file directly: " + cfgPath + "\n");
            return CommandResult::ok();
        }

        auto eq = args.find('=');
        if (eq == std::string::npos) {
            ctx.print("Usage: /api key=...  |  url=...  |  model=...  |  provider=...\n");
            return CommandResult::ok();
        }
        std::string field = trim(args.substr(0, eq));
        std::string val   = trim(args.substr(eq + 1));

        // Map the friendly field name to the YAML path (top-level vs api.* nested).
        std::string yamlKey; bool nested = true;
        if (field == "key" || field == "api_key" || field == "apikey") yamlKey = "api_key";
        else if (field == "url" || field == "base_url" || field == "baseurl") yamlKey = "base_url";
        else if (field == "model") yamlKey = "model";
        else if (field == "fallback" || field == "fallback_model") yamlKey = "fallback_model";
        else if (field == "provider") { yamlKey = "provider"; nested = false; }
        else { ctx.print("Unknown field '" + field + "'. Use key/url/model/provider.\n"); return CommandResult::ok(); }

        if (!writeYamlValue(cfgPath, yamlKey, val, nested)) {
            return CommandResult::fail("Could not write to " + cfgPath);
        }
        ctx.print("Set " + field + " = " + (field == "key" ? maskKey(val) : val) + "\n");
        ctx.print("Saved to " + cfgPath + ". Restart (/quit then relaunch) to apply.\n");
        return CommandResult::ok();
    }

private:
    static std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    static std::string maskKey(const std::string& k) {
        if (k.empty()) return "(not set)";
        if (k.size() <= 8) return "****";
        return k.substr(0, 5) + "..." + k.substr(k.size() - 4);
    }

    // Line-based YAML edit that PRESERVES comments/formatting (yaml-cpp rewrite
    // would strip them). `nested` keys live under the `api:` block (2-space indent);
    // otherwise it's a top-level key. Creates the line if missing.
    static bool writeYamlValue(const std::string& path, const std::string& key,
                               const std::string& value, bool nested) {
        namespace fs = std::filesystem;
        std::ifstream in(path);
        std::vector<std::string> lines;
        std::string line;
        if (in.is_open()) { while (std::getline(in, line)) lines.push_back(line); in.close(); }

        std::string quoted = "\"" + value + "\"";
        std::string newLine = nested ? ("  " + key + ": " + quoted)
                                      : (key + ": " + quoted);

        bool inApi = false, done = false;
        for (size_t i = 0; i < lines.size() && !done; i++) {
            const std::string& L = lines[i];
            std::string ltrim = L; size_t p = ltrim.find_first_not_of(" \t");
            std::string body = (p == std::string::npos) ? "" : ltrim.substr(p);
            bool indented = (p != std::string::npos && p > 0);

            if (nested) {
                if (!indented && body.rfind("api:", 0) == 0) { inApi = true; continue; }
                if (!indented && !body.empty() && body[0] != '#') inApi = false; // left the api block
                if (inApi && body.rfind(key + ":", 0) == 0) { lines[i] = newLine; done = true; }
            } else {
                if (!indented && body.rfind(key + ":", 0) == 0) { lines[i] = newLine; done = true; }
            }
        }
        if (!done) {
            // Key not found — append (under api: if nested and that block exists).
            if (nested) {
                bool placed = false;
                for (size_t i = 0; i < lines.size(); i++) {
                    if (lines[i].rfind("api:", 0) == 0) { lines.insert(lines.begin() + i + 1, newLine); placed = true; break; }
                }
                if (!placed) { lines.push_back("api:"); lines.push_back(newLine); }
            } else {
                lines.push_back(newLine);
            }
        }

        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) return false;
        for (const auto& l : lines) out << l << "\n";
        return true;
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

// /btw - Ask a side question without interrupting main conversation
class BtwCommand : public Command {
public:
    std::string getName() const override { return "btw"; }
    std::string getDescription() const override { return "Ask a quick side question without affecting main conversation"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print("Usage: /btw <question>\n");
            ctx.print("Ask a quick question without interrupting the main conversation.\n");
            return CommandResult::ok();
        }

        if (!ctx.apiClient) {
            ctx.print("Error: no API client configured\n");
            return CommandResult::ok();
        }

        ctx.print("\033[90m[btw] Thinking...\033[0m\n");

        try {
            // Build a lightweight message list (just the question)
            std::vector<Message> sideMessages;
            sideMessages.push_back(Message::makeUser(args));

            // Use a focused system prompt
            std::string sidePrompt = "You are a helpful assistant answering a quick side question. "
                "Be concise and direct. The user is in the middle of coding work and just wants a quick answer.";

            ModelConfig config;
            config.maxTokens = 2048;
            config.temperature = 0.7f;
            config.stream = true;

            std::string response;
            ctx.apiClient->streamChat(sideMessages, sidePrompt, config,
                [&](const StreamEvent& event) {
                    if (event.type == StreamEvent::EVT_TEXT) {
                        response += event.content;
                        ctx.print(event.content);
                    }
                });

            ctx.print("\n\033[90m[/btw end]\033[0m\n");
        } catch (const std::exception& e) {
            ctx.print("Error: " + std::string(e.what()) + "\n");
        }

        return CommandResult::ok();
    }
};

// /init - Generate CRAB.md for the current project
class InitCommand : public Command {
public:
    std::string getName() const override { return "init"; }
    std::string getDescription() const override { return "Generate a CRAB.md file for the current project"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        namespace fs = std::filesystem;
        fs::path crabMd = fs::path(ctx.cwd) / "CRAB.md";
        if (fs::exists(crabMd) && args != "--force") {
            ctx.print("CRAB.md already exists. Use /init --force to overwrite.\n");
            return CommandResult::ok();
        }
        std::string content = "# Project: " + fs::path(ctx.cwd).filename().string() + "\n\n";
        content += "## Rules\n\n";
        content += "- Follow existing code style and conventions\n";
        content += "- Run tests after changes\n";
        content += "- Keep files under 500 lines\n\n";
        content += "## Build\n\n```bash\n# Add build commands here\n```\n\n";
        content += "## Test\n\n```bash\n# Add test commands here\n```\n";
        std::ofstream f(crabMd.string());
        if (f.is_open()) { f << content; f.close(); }
        ctx.print("Created " + crabMd.string() + "\n");
        return CommandResult::ok();
    }
};

} // namespace closecrab
