#pragma once

#include "../Tool.h"
#include "../../utils/StringUtils.h"
#include "../../tools/TaskTools/TaskTools.h"
#include <array>
#include <memory>
#include <set>
#include <thread>
#include <future>
#include <chrono>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

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
                {"timeout", {{"type", "integer"}, {"description", "Timeout in milliseconds (max 600000)"}}},
                {"run_in_background", {{"type", "boolean"}, {"description", "Run in background, returns task ID"}}}
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
        std::string cmd = input["command"].get<std::string>();
        int timeout = input.value("timeout", 120000);
        bool background = input.value("run_in_background", false);

        if (cmd.empty()) return ToolResult::fail("Empty command");
        if (timeout > 600000) timeout = 600000;
        if (timeout < 1000) timeout = 1000;

        if (background) {
            return runInBackground(cmd, ctx);
        }

        return executeWithTimeout(cmd, timeout, ctx.abortFlag);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return input.value("command", "shell command");
    }

    bool isSearchOrRead(const nlohmann::json& input) const override {
        return isSafeCommand(input.value("command", ""));
    }

private:
    ToolResult runInBackground(const std::string& cmd, ToolContext& ctx) {
        // Create a task to track the background command
        auto& store = TaskStore::getInstance();
        std::string taskId = store.create("Bash: " + cmd.substr(0, 60), "Running: " + cmd);

        std::thread([cmd, taskId]() {
            std::string output;
#ifdef _WIN32
            std::string fullCmd = "cmd /c \"" + cmd + "\" 2>&1";
            std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(fullCmd.c_str(), "r"), _pclose);
#else
            std::string fullCmd = cmd + " 2>&1";
            std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(fullCmd.c_str(), "r"), pclose);
#endif
            if (pipe) {
                std::array<char, 4096> buffer;
                while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
                    output += buffer.data();
                    if (output.size() > 100 * 1024) {
                        output += "\n... (output truncated at 100KB)";
                        break;
                    }
                }
            }
            auto& store = TaskStore::getInstance();
            auto* task = store.get(taskId);
            if (task) {
                task->status = "completed";
                task->description = output.empty() ? "(no output)" : output;
            }
        }).detach();

        return ToolResult::ok("Background task started: " + taskId + "\nUse TaskOutput to check results.");
    }

    ToolResult executeWithTimeout(const std::string& cmd, int timeoutMs, std::atomic<bool>* abortFlag) {
#ifdef _WIN32
        return executeWithTimeoutWin32(cmd, timeoutMs, abortFlag);
#else
        return executeWithTimeoutPosix(cmd, timeoutMs, abortFlag);
#endif
    }

#ifdef _WIN32
    ToolResult executeWithTimeoutWin32(const std::string& cmd, int timeoutMs, std::atomic<bool>* abortFlag) {
        // Create pipes for stdout
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            return ToolResult::fail("Failed to create pipe");
        }
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        // Build command line
        std::string fullCmd = "cmd /c " + cmd;
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
            return ToolResult::fail("Failed to create process");
        }
        CloseHandle(hWritePipe); // Close write end in parent

        // Read output in a thread while waiting for process
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

        // Wait with timeout
        DWORD waitResult = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeoutMs));

        bool timedOut = false;
        if (waitResult == WAIT_TIMEOUT || (abortFlag && abortFlag->load())) {
            TerminateProcess(pi.hProcess, 1);
            timedOut = true;
        }

        // Wait for reader to finish
        if (reader.joinable()) reader.join();

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);

        if (timedOut) {
            output += "\n... (command timed out after " + std::to_string(timeoutMs / 1000) + "s)";
        }

        return ToolResult::ok(ensureUtf8(output));
    }
#else
    ToolResult executeWithTimeoutPosix(const std::string& cmd, int timeoutMs, std::atomic<bool>* abortFlag) {
        int pipefd[2];
        if (pipe(pipefd) == -1) return ToolResult::fail("Failed to create pipe");

        pid_t pid = fork();
        if (pid == -1) {
            close(pipefd[0]);
            close(pipefd[1]);
            return ToolResult::fail("Failed to fork");
        }

        if (pid == 0) {
            // Child
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            _exit(127);
        }

        // Parent
        close(pipefd[1]);

        std::string output;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        char buf[4096];
        bool timedOut = false;

        // Non-blocking read with timeout
        while (true) {
            if (std::chrono::steady_clock::now() >= deadline || (abortFlag && abortFlag->load())) {
                kill(pid, SIGKILL);
                timedOut = true;
                break;
            }

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(pipefd[0], &fds);
            struct timeval tv = {0, 100000}; // 100ms poll
            int ret = select(pipefd[0] + 1, &fds, nullptr, nullptr, &tv);
            if (ret > 0) {
                ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
                if (n <= 0) break;
                buf[n] = '\0';
                output += buf;
                if (output.size() > 100 * 1024) {
                    output += "\n... (output truncated at 100KB)";
                    kill(pid, SIGKILL);
                    break;
                }
            } else if (ret == 0) {
                // Check if child exited
                int status;
                pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid) { pid = 0; break; }
            }
        }

        close(pipefd[0]);
        if (pid > 0) waitpid(pid, nullptr, 0);

        if (timedOut) {
            output += "\n... (command timed out after " + std::to_string(timeoutMs / 1000) + "s)";
        }

        return ToolResult::ok(ensureUtf8(output));
    }
#endif

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