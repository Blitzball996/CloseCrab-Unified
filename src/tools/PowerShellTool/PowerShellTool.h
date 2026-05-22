#pragma once

#include "../Tool.h"
#include <cstdio>
#include <array>
#include <memory>
#include <set>
#include <vector>
#include <thread>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

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
        int timeout = input.value("timeout", 120000);
        if (timeout > 600000) timeout = 600000;
        if (timeout < 1000) timeout = 1000;

        // Use pwsh (PowerShell 7) if available, fallback to powershell.exe
        // Use -EncodedCommand to avoid quoting issues with complex commands
        std::string encoded = encodeForPowerShell(cmd);
        std::string fullCmd = "powershell.exe -NoProfile -NonInteractive -EncodedCommand " + encoded;

        // Execute with timeout using CreateProcess (same pattern as BashTool)
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            return ToolResult::fail("Failed to create pipe");
        }
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        std::vector<char> cmdBuf(fullCmd.begin(), fullCmd.end());
        cmdBuf.push_back('\0');

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        PROCESS_INFORMATION pi = {};

        if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            return ToolResult::fail("Failed to start PowerShell process");
        }
        CloseHandle(hWritePipe);

        // Read output with timeout
        std::string output;
        std::atomic<bool> readDone{false};
        std::thread reader([&]() {
            char buf[4096];
            DWORD bytesRead;
            while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                buf[bytesRead] = '\0';
                output += buf;
                if (output.size() > 100 * 1024) {
                    output += "\n... (output truncated at 100KB)";
                    break;
                }
            }
            readDone = true;
        });

        DWORD waitResult = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout));
        bool timedOut = false;
        if (waitResult == WAIT_TIMEOUT || (ctx.abortFlag && ctx.abortFlag->load())) {
            TerminateProcess(pi.hProcess, 1);
            timedOut = true;
        }

        if (reader.joinable()) reader.join();

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);

        if (timedOut) {
            output += "\n... (command timed out after " + std::to_string(timeout / 1000) + "s)";
        }

        // Strip CLIXML progress noise from PowerShell output
        output = stripCliXml(output);

        return ToolResult::ok(output);
#endif
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "PowerShell: " + input.value("command", "...");
    }

private:
    // Strip CLIXML progress/error markup from PowerShell output
    static std::string stripCliXml(const std::string& output) {
        std::string result;
        result.reserve(output.size());
        size_t i = 0;
        while (i < output.size()) {
            // Skip "#< CLIXML" lines
            if (i == 0 || (i > 0 && output[i-1] == '\n')) {
                if (output.compare(i, 9, "#< CLIXML") == 0) {
                    while (i < output.size() && output[i] != '\n') i++;
                    if (i < output.size()) i++; // skip newline
                    continue;
                }
            }
            // Skip <Objs>...</Objs> XML blocks
            if (output.compare(i, 5, "<Objs") == 0) {
                size_t end = output.find("</Objs>", i);
                if (end != std::string::npos) {
                    i = end + 7;
                    if (i < output.size() && output[i] == '\n') i++;
                    continue;
                }
            }
            result += output[i];
            i++;
        }
        // Trim trailing whitespace
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
            result.pop_back();
        }
        return result;
    }

    // Encode command as Base64 UTF-16LE for -EncodedCommand (avoids all quoting issues)
    static std::string encodeForPowerShell(const std::string& cmd) {
        // Convert UTF-8 to UTF-16LE
        std::vector<uint8_t> utf16;
        for (size_t i = 0; i < cmd.size(); i++) {
            utf16.push_back(static_cast<uint8_t>(cmd[i]));
            utf16.push_back(0); // UTF-16LE: ASCII char + 0x00
        }
        // Base64 encode
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        for (size_t i = 0; i < utf16.size(); i += 3) {
            uint32_t n = (uint32_t)utf16[i] << 16;
            if (i + 1 < utf16.size()) n |= (uint32_t)utf16[i + 1] << 8;
            if (i + 2 < utf16.size()) n |= (uint32_t)utf16[i + 2];
            encoded += b64[(n >> 18) & 0x3F];
            encoded += b64[(n >> 12) & 0x3F];
            encoded += (i + 1 < utf16.size()) ? b64[(n >> 6) & 0x3F] : '=';
            encoded += (i + 2 < utf16.size()) ? b64[n & 0x3F] : '=';
        }
        return encoded;
    }

    static bool isSafeCommand(const std::string& cmd) {
        static const std::set<std::string> safe = {
            "Get-", "Select-", "Where-", "Format-", "Out-", "Write-",
            "Test-", "Measure-", "Compare-", "ConvertTo-", "ConvertFrom-"
        };
        for (const auto& prefix : safe) {
            if (cmd.find(prefix) == 0) return true;
        }
        if (cmd.find("Get-ChildItem") != std::string::npos) return true;
        if (cmd.find("Get-Content") != std::string::npos) return true;
        if (cmd.find("Get-Process") != std::string::npos) return true;
        if (cmd.find("$PSVersionTable") != std::string::npos) return true;
        return false;
    }
};

} // namespace closecrab
