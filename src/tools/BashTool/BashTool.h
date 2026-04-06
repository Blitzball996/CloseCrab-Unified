#pragma once

#include "../Tool.h"
#include <cstdio>
#include <array>
#include <memory>
#include <regex>
#include <set>

namespace closecrab {

class BashTool : public Tool {
public:
    std::string getName() const override { return "Bash"; }
    std::string getDescription() const override {
        return "Execute a shell command and return its output. "
               "Destructive commands require confirmation.";
    }
    std::string getCategory() const override { return "execution"; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"command", {{"type", "string"}, {"description", "Shell command to execute"}}},
                {"description", {{"type", "string"}, {"description", "What this command does"}}},
                {"timeout", {{"type", "integer"}, {"description", "Timeout in milliseconds (max 600000)"}}}
            }},
            {"required", {"command"}}
        };
    }

    bool isDestructive() const override { return true; }

    PermissionResult checkPermissions(const ToolContext& ctx, const nlohmann::json& input) const override {
        std::string cmd = input.value("command", "");
        // Read-only commands are safe
        if (isSafeCommand(cmd)) return PermissionResult::ALLOWED;
        return PermissionResult::ASK_USER;
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string cmd = input["command"].get<std::string>();
        std::string desc = input.value("description", "");
        int timeout = input.value("timeout", 120000);

        if (cmd.empty()) return ToolResult::fail("Empty command");

        // Execute
        std::string output;
        int exitCode = 0;

#ifdef _WIN32
        std::string fullCmd = "cmd /c \"" + cmd + "\" 2>&1";
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(fullCmd.c_str(), "r"), _pclose);
#else
        std::string fullCmd = cmd + " 2>&1";
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(fullCmd.c_str(), "r"), pclose);
#endif
        if (!pipe) return ToolResult::fail("Failed to execute command");

        std::array<char, 4096> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            output += buffer.data();
            if (ctx.abortFlag && ctx.abortFlag->load()) break;
            // Truncate at 100KB
            if (output.size() > 100 * 1024) {
                output += "\n... (output truncated at 100KB)";
                break;
            }
        }

        return ToolResult::ok(output);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return input.value("command", "shell command");
    }

    bool isSearchOrRead(const nlohmann::json& input) const override {
        return isSafeCommand(input.value("command", ""));
    }

private:
    static bool isSafeCommand(const std::string& cmd) {
        static const std::set<std::string> safeCommands = {
            "ls", "dir", "cat", "head", "tail", "echo", "pwd", "whoami",
            "date", "time", "type", "find", "grep", "rg", "git status",
            "git log", "git diff", "git branch", "npm test", "cmake"
        };
        for (const auto& safe : safeCommands) {
            if (cmd.substr(0, safe.size()) == safe) return true;
        }
        return false;
    }
};

} // namespace closecrab
