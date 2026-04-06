#pragma once

#include "../Tool.h"
#include <cstdio>
#include <array>
#include <memory>
#include <set>

namespace closecrab {

class PowerShellTool : public Tool {
public:
    std::string getName() const override { return "PowerShell"; }
    std::string getDescription() const override {
        return "Execute a PowerShell command on Windows and return its output.";
    }
    std::string getCategory() const override { return "execution"; }

    bool isEnabled() const override {
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"command", {{"type", "string"}, {"description", "PowerShell command to execute"}}},
                {"description", {{"type", "string"}, {"description", "What this command does"}}},
                {"timeout", {{"type", "integer"}, {"description", "Timeout in milliseconds (max 600000)"}}}
            }},
            {"required", {"command"}}
        };
    }

    bool isDestructive() const override { return true; }

    PermissionResult checkPermissions(const ToolContext& ctx, const nlohmann::json& input) const override {
        std::string cmd = input.value("command", "");
        if (isSafeCommand(cmd)) return PermissionResult::ALLOWED;
        return PermissionResult::ASK_USER;
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
#ifndef _WIN32
        return ToolResult::fail("PowerShell is only available on Windows");
#else
        std::string cmd = input["command"].get<std::string>();
        if (cmd.empty()) return ToolResult::fail("Empty command");

        // Use powershell.exe with -NoProfile for speed, -Command for inline execution
        std::string fullCmd = "powershell.exe -NoProfile -NonInteractive -Command \""
                              + escapeForPowerShell(cmd) + "\" 2>&1";

        std::string output;
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(fullCmd.c_str(), "r"), _pclose);
        if (!pipe) return ToolResult::fail("Failed to start PowerShell");

        std::array<char, 4096> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            output += buffer.data();
            if (ctx.abortFlag && ctx.abortFlag->load()) break;
            if (output.size() > 100 * 1024) {
                output += "\n... (output truncated at 100KB)";
                break;
            }
        }

        return ToolResult::ok(output);
#endif
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "PowerShell: " + input.value("command", "...");
    }

private:
    static std::string escapeForPowerShell(const std::string& cmd) {
        std::string escaped;
        for (char c : cmd) {
            if (c == '"') escaped += "\\\"";
            else escaped += c;
        }
        return escaped;
    }

    static bool isSafeCommand(const std::string& cmd) {
        static const std::set<std::string> safe = {
            "Get-", "Select-", "Where-", "Format-", "Out-", "Write-",
            "Test-", "Measure-", "Compare-", "ConvertTo-", "ConvertFrom-"
        };
        for (const auto& prefix : safe) {
            if (cmd.find(prefix) == 0) return true;
        }
        // Also safe: simple queries
        if (cmd.find("Get-ChildItem") != std::string::npos) return true;
        if (cmd.find("Get-Content") != std::string::npos) return true;
        if (cmd.find("Get-Process") != std::string::npos) return true;
        if (cmd.find("$PSVersionTable") != std::string::npos) return true;
        return false;
    }
};

} // namespace closecrab
