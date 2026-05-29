#pragma once

#include "../Tool.h"
#include "../../utils/StringUtils.h"
#include "../../utils/PathValidation.h"
#include "../../utils/ShellQuoting.h"
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
        return "Execute a shell command. IMPORTANT — use dedicated tools instead of these commands:\n"
               "- File search: use Glob (NOT find/dir/ls)\n"
               "- Content search: use Grep (NOT grep/rg/findstr)\n"
               "- Read files: use Read (NOT cat/head/tail/type)\n"
               "- Edit files: use Edit (NOT sed/awk)\n"
               "- Write files: use Write (NOT echo >/cat <<EOF)\n"
               "On Windows (Git Bash): use POSIX paths (/g/CMakePJ/...) NOT G:\\...; "
               "NEVER call Windows native commands (findstr, cmd builtins) — their /flag gets mangled to I:/, N:/. "
               "Prefer writing a temp script file over inline node -e / python -c / sed -i (shell quote mangling). "
               "Destructive commands (rm, del, rmdir) require confirmation.";
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
        // §5: Dangerous rm path detection now lives in PermissionEngine::check()
        // (JackProAi pathValidation.ts:713-737 flow). Tool layer just returns ASK_USER.
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

        return executeWithTimeout(cmd, timeout, ctx.abortFlag, ctx.onStreamOutput, ctx.cwd, ctx.sessionCwd);
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
            // Apply the same JackProAi normalization (>nul rewrite + eval wrap).
            // NOTE: background uses _popen which adds a cmd.exe layer; the bash
            // layer is now quote-safe via eval single-quoting. Foreground path
            // (executeWithTimeoutWin32) is fully Windows-argv-escaped.
            std::string normalized = rewriteWindowsNullRedirect(cmd);
            std::string evalCmd = buildEvalCommand(normalized);
            std::string fullCmd = "\"C:/Program Files/Git/bin/bash.exe\" -c " +
                                  windowsQuoteArg(evalCmd) + " 2>&1";
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

    ToolResult executeWithTimeout(const std::string& cmd, int timeoutMs,
                                   std::atomic<bool>* abortFlag,
                                   std::function<void(const std::string&)> streamCb = nullptr,
                                   const std::string& cwd = "",
                                   std::string* sessionCwdOut = nullptr) {
#ifdef _WIN32
        return executeWithTimeoutWin32(cmd, timeoutMs, abortFlag, streamCb, cwd, sessionCwdOut);
#else
        return executeWithTimeoutPosix(cmd, timeoutMs, abortFlag);
#endif
    }

#ifdef _WIN32
    ToolResult executeWithTimeoutWin32(const std::string& cmd, int timeoutMs,
                                       std::atomic<bool>* abortFlag,
                                       std::function<void(const std::string&)> streamCb,
                                       const std::string& cwd = "",
                                       std::string* sessionCwdOut = nullptr) {
        // Create pipes for stdout
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            return ToolResult::fail("Failed to create pipe");
        }
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        // §10: Auto-discover Git Bash path
        static std::string gitBashPath;
        if (gitBashPath.empty()) {
            const char* envPath = std::getenv("CLAUDE_CODE_GIT_BASH_PATH");
            if (envPath && std::filesystem::exists(envPath)) {
                gitBashPath = envPath;
            } else {
                // Default locations
                std::vector<std::string> candidates = {
                    "C:/Program Files/Git/bin/bash.exe",
                    "C:/Program Files (x86)/Git/bin/bash.exe",
                    "D:/Program Files/Git/bin/bash.exe",
                };
                for (const auto& c : candidates) {
                    if (std::filesystem::exists(c)) { gitBashPath = c; break; }
                }
                if (gitBashPath.empty()) gitBashPath = "C:/Program Files/Git/bin/bash.exe";
            }
        }

        // §10: Build command line the JackProAi way (shellQuoting.ts + bashProvider.ts):
        //   1. rewrite Windows `>nul` redirects → /dev/null
        //   2. wrap in `eval '<single-quote-escaped>'` so inner quotes survive
        //   3. escape the whole eval string as ONE Windows argv element
        // This fixes the bug.txt failures where `node -e "..."` / `sed -i "..."`
        // inner double quotes shredded the outer -c "..." wrapper.
        std::string normalized = rewriteWindowsNullRedirect(cmd);
        std::string evalCmd = buildEvalCommand(normalized);

        // §10: cwd persistence (JackProAi bashProvider.ts: `eval ... && pwd -P >| file`).
        // Append a pwd capture that ALWAYS runs (`;` not `&&`) and preserves the
        // real exit code, so `cd subdir` carries to the next bash call.
        std::string cwdFile;
        if (sessionCwdOut) {
            char tmp[MAX_PATH];
            DWORD tlen = GetTempPathA(MAX_PATH, tmp);
            if (tlen > 0 && tlen < MAX_PATH) {
                cwdFile = std::string(tmp) + "crab-cwd-" +
                          std::to_string(GetCurrentProcessId()) + "-" +
                          std::to_string((unsigned long long)GetTickCount64()) + ".txt";
                // POSIX path for the bash redirect (C:\ -> /c/)
                std::string posixCwdFile = windowsToPosixPath(cwdFile);
                evalCmd += "; __cc_ec=$?; pwd -P > " + bashSingleQuote(posixCwdFile) +
                           " 2>/dev/null; exit $__cc_ec";
            }
        }

        std::string fullCmd = windowsQuoteArg(gitBashPath) + " -c " + windowsQuoteArg(evalCmd);
        std::vector<char> cmdBuf(fullCmd.begin(), fullCmd.end());
        cmdBuf.push_back('\0');

        // §10: Build environment block with CLAUDECODE, GIT_EDITOR, LANG for UTF-8
        std::string envBlock;
        // Inherit current environment
        char* currentEnv = GetEnvironmentStringsA();
        if (currentEnv) {
            char* p = currentEnv;
            while (*p) {
                envBlock.append(p);
                envBlock.push_back('\0');
                p += strlen(p) + 1;
            }
            FreeEnvironmentStringsA(currentEnv);
        }
        envBlock += "CLAUDECODE=1"; envBlock.push_back('\0');
        envBlock += "GIT_EDITOR=true"; envBlock.push_back('\0');
        envBlock += "SHELL=" + gitBashPath; envBlock.push_back('\0');
        // LANG: not set by JackProAi, but CloseCrab has CJK paths/content that
        // need UTF-8 locale to round-trip cleanly through bash. Harmless add-on.
        envBlock += "LANG=en_US.UTF-8"; envBlock.push_back('\0');
        envBlock.push_back('\0'); // Double null terminator

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        PROCESS_INFORMATION pi = {};

        // §10: Pass cwd as lpCurrentDirectory, env block as lpEnvironment
        const char* lpCwd = cwd.empty() ? nullptr : cwd.c_str();

        if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, envBlock.data(), lpCwd, &si, &pi)) {
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
                // Stream output line-by-line to UI
                if (streamCb) {
                    streamCb(std::string(buf, bytesRead));
                }
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

        // §10: cwd persistence — read back the `pwd -P` capture and update the
        // session cwd so the next bash call inherits any `cd`.
        if (sessionCwdOut && !cwdFile.empty()) {
            std::ifstream cf(cwdFile, std::ios::binary);
            if (cf) {
                std::string newCwd((std::istreambuf_iterator<char>(cf)), {});
                cf.close();
                while (!newCwd.empty() &&
                       (newCwd.back() == '\n' || newCwd.back() == '\r' || newCwd.back() == ' '))
                    newCwd.pop_back();
                if (!newCwd.empty()) {
                    // bash emits a POSIX path (/g/...); CreateProcessA's
                    // lpCurrentDirectory needs a Windows path (G:\...).
                    *sessionCwdOut = posixToWindowsPath(newCwd);
                }
            }
            std::remove(cwdFile.c_str());
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
            "git log", "git diff", "git branch", "npm test", "cmake",
            "mkdir", "cd", "which", "where", "python --version",
            "node --version", "npm --version", "pip --version"
        };
        for (const auto& safe : safeCommands) {
            if (cmd.size() >= safe.size() && cmd.substr(0, safe.size()) == safe) {
                // Must be exact match or followed by space/end (not a prefix of a longer word)
                if (cmd.size() == safe.size() || cmd[safe.size()] == ' ') return true;
            }
        }
        return false;
    }
};

} // namespace closecrab