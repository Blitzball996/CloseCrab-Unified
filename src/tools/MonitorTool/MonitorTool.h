#pragma once

#include "../Tool.h"
#include "../TaskTools/TaskTools.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace closecrab {

class MonitorTool : public Tool {
public:
    std::string getName() const override { return "Monitor"; }
    std::string getDescription() const override {
        return "Start a background monitor that streams events from a long-running script. "
               "Each stdout line is an event notification. Exit ends the watch.";
    }
    std::string getCategory() const override { return "execution"; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"command", {{"type", "string"}, {"description", "Shell command to monitor"}}},
                {"description", {{"type", "string"}, {"description", "What you are monitoring"}}},
                {"timeout_ms", {{"type", "integer"}, {"description", "Kill after this many ms (default 300000)"}}},
                {"persistent", {{"type", "boolean"}, {"description", "Run for session lifetime (no timeout)"}}}
            }},
            {"required", {"command"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string cmd = input["command"].get<std::string>();
        std::string desc = input.value("description", cmd.substr(0, 60));
        int timeoutMs = jsonInt(input, "timeout_ms", 300000);
        bool persistent = jsonBool(input, "persistent", false);
        if (persistent) timeoutMs = 3600000;

        auto& store = TaskStore::getInstance();
        std::string taskId = store.create("Monitor: " + desc, "");

        std::thread([cmd, taskId, timeoutMs, desc]() {
            auto& store = TaskStore::getInstance();
            auto* task = store.get(taskId);
            if (!task) return;
            task->status = "running";

#ifdef _WIN32
            std::string fullCmd = "cmd /c " + cmd + " 2>&1";
            SECURITY_ATTRIBUTES sa = {};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            HANDLE hRead, hWrite;
            if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
                task->status = "failed";
                task->description = "Failed to create pipe";
                return;
            }
            SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

            std::vector<char> cmdBuf(fullCmd.begin(), fullCmd.end());
            cmdBuf.push_back('\0');
            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = hWrite;
            si.hStdError = hWrite;
            PROCESS_INFORMATION pi = {};

            if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(hRead);
                CloseHandle(hWrite);
                task->status = "failed";
                task->description = "Failed to start process";
                return;
            }
            CloseHandle(hWrite);

            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            std::string output;
            char buf[1024];
            DWORD bytesRead;

            while (true) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    TerminateProcess(pi.hProcess, 1);
                    output += "\n[Monitor timed out after " + std::to_string(timeoutMs/1000) + "s]";
                    break;
                }
                if (task->status == "stopped") {
                    TerminateProcess(pi.hProcess, 1);
                    output += "\n[Monitor stopped by user]";
                    break;
                }

                DWORD avail = 0;
                PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr);
                if (avail > 0) {
                    if (ReadFile(hRead, buf, sizeof(buf)-1, &bytesRead, nullptr) && bytesRead > 0) {
                        buf[bytesRead] = '\0';
                        output += buf;
                        task->description = output;
                        if (output.size() > 200*1024) {
                            output = output.substr(output.size() - 100*1024);
                            task->description = output;
                        }
                    }
                } else {
                    DWORD exitCode;
                    if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }

            WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hRead);
#else
            std::string output = "(Monitor not implemented on this platform)";
#endif
            if (task->status != "stopped") {
                task->status = "completed";
            }
            if (task->description.empty()) {
                task->description = "(no output)";
            }
        }).detach();

        return ToolResult::ok("Monitor started: " + taskId + "\nDescription: " + desc +
            "\nUse TaskOutput to check results, TaskStop to end.");
    }
};

} // namespace closecrab
