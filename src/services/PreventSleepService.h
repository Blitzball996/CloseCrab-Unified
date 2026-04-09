#pragma once

#include <mutex>
#include <atomic>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#endif

namespace closecrab {

class PreventSleepService {
public:
    static PreventSleepService& getInstance() {
        static PreventSleepService instance;
        return instance;
    }

    void acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (refCount_++ > 0) {
            spdlog::debug("PreventSleep: already acquired (refCount={})", refCount_.load());
            return;
        }

        spdlog::info("PreventSleep: acquiring sleep prevention");

#ifdef _WIN32
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        spdlog::debug("PreventSleep: SetThreadExecutionState set");
#elif defined(__APPLE__)
        caffeinatePid_ = fork();
        if (caffeinatePid_ == 0) {
            execlp("caffeinate", "caffeinate", "-i", nullptr);
            _exit(1);
        }
        spdlog::debug("PreventSleep: caffeinate started (pid={})", caffeinatePid_);
#else
        // Linux: try systemd-inhibit or xdg-screensaver
        int ret = std::system("systemd-inhibit --what=idle --who=CloseCrab "
                              "--why='Long operation' sleep infinity &");
        if (ret != 0) {
            ret = std::system("xdg-screensaver suspend $PPID 2>/dev/null &");
        }
        (void)ret;
        spdlog::debug("PreventSleep: Linux sleep inhibitor started");
#endif
    }

    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (refCount_ <= 0) {
            spdlog::debug("PreventSleep: not acquired, nothing to release");
            return;
        }
        if (--refCount_ > 0) {
            spdlog::debug("PreventSleep: still held (refCount={})", refCount_.load());
            return;
        }

        spdlog::info("PreventSleep: releasing sleep prevention");

#ifdef _WIN32
        SetThreadExecutionState(ES_CONTINUOUS);
        spdlog::debug("PreventSleep: SetThreadExecutionState cleared");
#elif defined(__APPLE__)
        if (caffeinatePid_ > 0) {
            kill(caffeinatePid_, SIGTERM);
            spdlog::debug("PreventSleep: caffeinate terminated (pid={})", caffeinatePid_);
            caffeinatePid_ = -1;
        }
#else
        // Linux: kill background inhibitors
        std::system("pkill -f 'systemd-inhibit.*CloseCrab' 2>/dev/null");
        spdlog::debug("PreventSleep: Linux sleep inhibitor stopped");
#endif
    }

    bool isAcquired() const {
        return refCount_ > 0;
    }

    // RAII guard for automatic acquire/release
    class SleepGuard {
    public:
        SleepGuard() {
            PreventSleepService::getInstance().acquire();
        }

        ~SleepGuard() {
            PreventSleepService::getInstance().release();
        }

        // Non-copyable, non-movable
        SleepGuard(const SleepGuard&) = delete;
        SleepGuard& operator=(const SleepGuard&) = delete;
        SleepGuard(SleepGuard&&) = delete;
        SleepGuard& operator=(SleepGuard&&) = delete;
    };

private:
    PreventSleepService() = default;
    std::mutex mutex_;
    std::atomic<int> refCount_{0};

#ifdef __APPLE__
    pid_t caffeinatePid_ = -1;
#endif
};

} // namespace closecrab
