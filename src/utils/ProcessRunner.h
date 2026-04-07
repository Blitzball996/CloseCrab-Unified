#pragma once

#include <string>
#include <functional>
#include <chrono>
#include <atomic>
#include <thread>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/select.h>
#endif

namespace closecrab {

struct ProcessResult {
    int exitCode = -1;
    std::string output;
    bool timedOut = false;
    bool killed = false;
};

// Cross-platform process execution with timeout and output capture
class ProcessRunner {
public:
    static ProcessResult run(const std::string& command, int timeoutMs = 120000,
                              std::atomic<bool>* abortFlag = nullptr,
                              size_t maxOutputBytes = 100 * 1024) {
#ifdef _WIN32
        return runWindows(command, timeoutMs, abortFlag, maxOutputBytes);
#else
        return runPosix(command, timeoutMs, abortFlag, maxOutputBytes);
#endif
    }

    // Run command and stream output line by line
    static ProcessResult runStreaming(const std::string& command, int timeoutMs,
                                      std::function<void(const std::string&)> onLine,
                                      std::atomic<bool>* abortFlag = nullptr) {
        ProcessResult result = run(command, timeoutMs, abortFlag);
        if (onLine && !result.output.empty()) {
            std::istringstream iss(result.output);
            std::string line;
            while (std::getline(iss, line)) {
                onLine(line);
            }
        }
        return result;
    }

    // Build a shell command string appropriate for the platform
    static std::string shellWrap(const std::string& cmd) {
#ifdef _WIN32
        return "cmd /c \"" + cmd + "\"";
#else
        return "/bin/sh -c '" + escapeForShell(cmd) + "'";
#endif
    }

private:
#ifdef _WIN32
    static ProcessResult runWindows(const std::string& cmd, int timeoutMs,
                                     std::atomic<bool>* abortFlag, size_t maxOutput) {
        ProcessResult result;

        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            result.output = "Failed to create pipe";
            return result;
        }
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

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
            result.output = "Failed to create process";
            return result;
        }
        CloseHandle(hWritePipe);

        // Read output in thread
        std::thread reader([&]() {
            char buf[4096];
            DWORD bytesRead;
            while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                buf[bytesRead] = '\0';
                result.output += buf;
                if (result.output.size() > maxOutput) {
                    result.output += "\n...(truncated)";
                    break;
                }
            }
        });

        DWORD waitResult = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeoutMs));
        if (waitResult == WAIT_TIMEOUT || (abortFlag && abortFlag->load())) {
            TerminateProcess(pi.hProcess, 1);
            result.timedOut = (waitResult == WAIT_TIMEOUT);
            result.killed = true;
        }

        if (reader.joinable()) reader.join();

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        result.exitCode = static_cast<int>(exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
        return result;
    }
#else
    static ProcessResult runPosix(const std::string& cmd, int timeoutMs,
                                   std::atomic<bool>* abortFlag, size_t maxOutput) {
        ProcessResult result;

        int pipefd[2];
        if (pipe(pipefd) == -1) {
            result.output = "Failed to create pipe";
            return result;
        }

        pid_t pid = fork();
        if (pid == -1) {
            close(pipefd[0]);
            close(pipefd[1]);
            result.output = "Failed to fork";
            return result;
        }

        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            _exit(127);
        }

        close(pipefd[1]);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        char buf[4096];

        while (true) {
            if (std::chrono::steady_clock::now() >= deadline || (abortFlag && abortFlag->load())) {
                kill(pid, SIGKILL);
                result.timedOut = true;
                result.killed = true;
                break;
            }

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(pipefd[0], &fds);
            struct timeval tv = {0, 100000};
            int ret = select(pipefd[0] + 1, &fds, nullptr, nullptr, &tv);
            if (ret > 0) {
                ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
                if (n <= 0) break;
                buf[n] = '\0';
                result.output += buf;
                if (result.output.size() > maxOutput) {
                    result.output += "\n...(truncated)";
                    kill(pid, SIGKILL);
                    break;
                }
            } else if (ret == 0) {
                int status;
                pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid) {
                    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                    pid = 0;
                    break;
                }
            }
        }

        close(pipefd[0]);
        if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        return result;
    }

    static std::string escapeForShell(const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '\'') result += "'\\''";
            else result += c;
        }
        return result;
    }
#endif
};

} // namespace closecrab
