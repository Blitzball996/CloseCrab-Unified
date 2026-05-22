#pragma once
#include <chrono>
#include <atomic>
#include <string>

namespace closecrab {

class AssistantMode {
public:
    static constexpr int AUTO_BACKGROUND_THRESHOLD_MS = 15000;

    static bool isEnabled() { return enabled_; }
    static void setEnabled(bool e) { enabled_ = e; }

    // Check if a command has been running too long and should be backgrounded
    static bool shouldAutoBackground(std::chrono::steady_clock::time_point startTime) {
        if (!enabled_) return false;
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > AUTO_BACKGROUND_THRESHOLD_MS;
    }

private:
    static inline bool enabled_ = false;
};

} // namespace closecrab
