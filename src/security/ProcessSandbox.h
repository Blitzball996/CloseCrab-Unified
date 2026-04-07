#pragma once

#include <string>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace closecrab {

// OS-level process resource limits for sandboxed execution
struct ProcessLimits {
    size_t maxMemoryMB = 512;       // Max memory in MB (0 = unlimited)
    int maxCpuSeconds = 60;         // Max CPU time in seconds (0 = unlimited)
    size_t maxOutputBytes = 1024 * 1024; // Max output capture (1MB)
    bool allowNetwork = true;       // Allow network access
    std::string allowedDir;         // Restrict filesystem to this directory (empty = no restriction)
};

class ProcessSandbox {
public:
    static ProcessSandbox& getInstance() {
        static ProcessSandbox instance;
        return instance;
    }

    void setLimits(const ProcessLimits& limits) { limits_ = limits; }
    const ProcessLimits& getLimits() const { return limits_; }

#ifdef _WIN32
    // Create a Job Object with resource limits and assign a process to it
    HANDLE createLimitedJob() {
        HANDLE job = CreateJobObjectA(nullptr, nullptr);
        if (!job) {
            spdlog::warn("Failed to create Job Object: {}", GetLastError());
            return nullptr;
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = 0;

        // Memory limit
        if (limits_.maxMemoryMB > 0) {
            jeli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
            jeli.ProcessMemoryLimit = limits_.maxMemoryMB * 1024 * 1024;
        }

        // CPU time limit
        if (limits_.maxCpuSeconds > 0) {
            jeli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME;
            // PerProcessUserTimeLimit is in 100-nanosecond intervals
            jeli.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart =
                static_cast<LONGLONG>(limits_.maxCpuSeconds) * 10000000LL;
        }

        // Kill processes when job is closed
        jeli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                      &jeli, sizeof(jeli))) {
            spdlog::warn("Failed to set Job Object limits: {}", GetLastError());
            CloseHandle(job);
            return nullptr;
        }

        spdlog::debug("Created sandboxed Job Object (mem: {}MB, cpu: {}s)",
                       limits_.maxMemoryMB, limits_.maxCpuSeconds);
        return job;
    }

    bool assignProcessToJob(HANDLE job, HANDLE process) {
        if (!job || !process) return false;
        if (!AssignProcessToJobObject(job, process)) {
            spdlog::warn("Failed to assign process to Job Object: {}", GetLastError());
            return false;
        }
        return true;
    }
#else
    // Apply resource limits to current process (call after fork, before exec)
    void applyLimitsToChild() {
        // Memory limit
        if (limits_.maxMemoryMB > 0) {
            struct rlimit rl;
            rl.rlim_cur = rl.rlim_max = limits_.maxMemoryMB * 1024 * 1024;
            setrlimit(RLIMIT_AS, &rl);
        }

        // CPU time limit
        if (limits_.maxCpuSeconds > 0) {
            struct rlimit rl;
            rl.rlim_cur = rl.rlim_max = limits_.maxCpuSeconds;
            setrlimit(RLIMIT_CPU, &rl);
        }

        // File size limit (prevent filling disk)
        {
            struct rlimit rl;
            rl.rlim_cur = rl.rlim_max = 100 * 1024 * 1024; // 100MB max file write
            setrlimit(RLIMIT_FSIZE, &rl);
        }
    }
#endif

    // Check if a command is allowed under current sandbox rules
    bool isCommandAllowed(const std::string& cmd) const {
        // Block obviously dangerous patterns
        static const std::string blocked[] = {
            "rm -rf /", "mkfs", "dd if=/dev/zero", ":(){ :|:& };:",
            "chmod -R 777 /", "format c:", "del /f /s /q c:\\"
        };
        for (const auto& pattern : blocked) {
            if (cmd.find(pattern) != std::string::npos) {
                spdlog::warn("Sandbox blocked dangerous command: {}", cmd.substr(0, 80));
                return false;
            }
        }
        return true;
    }

    // Check if a file path is within allowed directory
    bool isPathAllowed(const std::string& path) const {
        if (limits_.allowedDir.empty()) return true;
        // Simple prefix check (not foolproof against symlinks)
        return path.find(limits_.allowedDir) == 0;
    }

    std::string getSummary() const {
        std::string s;
        s += "Memory limit: " + (limits_.maxMemoryMB > 0 ?
            std::to_string(limits_.maxMemoryMB) + "MB" : "unlimited") + "\n";
        s += "CPU limit: " + (limits_.maxCpuSeconds > 0 ?
            std::to_string(limits_.maxCpuSeconds) + "s" : "unlimited") + "\n";
        s += "Network: " + std::string(limits_.allowNetwork ? "allowed" : "blocked") + "\n";
        s += "Directory: " + (limits_.allowedDir.empty() ? "unrestricted" : limits_.allowedDir) + "\n";
        return s;
    }

private:
    ProcessSandbox() = default;
    ProcessLimits limits_;
};

} // namespace closecrab
