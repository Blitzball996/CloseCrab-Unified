#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace closecrab {
namespace fs = std::filesystem;

// Local file-based analytics (no cloud dependency)
class AnalyticsService {
public:
    static AnalyticsService& getInstance() {
        static AnalyticsService instance;
        return instance;
    }

    void setEnabled(bool v) { enabled_ = v; }
    bool isEnabled() const { return enabled_; }

    void setLogDir(const std::string& dir) {
        logDir_ = dir;
        if (!fs::exists(logDir_)) fs::create_directories(logDir_);
    }

    // Log an event
    void logEvent(const std::string& event, const nlohmann::json& data = {}) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lock(mutex_);

        nlohmann::json entry;
        entry["event"] = event;
        entry["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (!data.is_null()) entry["data"] = data;

        buffer_.push_back(std::move(entry));

        // Flush every 50 events
        if (buffer_.size() >= 50) flushInternal();
    }

    // Query checkpoint (for profiling)
    void queryCheckpoint(const std::string& name, double durationMs) {
        logEvent("query_checkpoint", {{"name", name}, {"duration_ms", durationMs}});
    }

    // Flush buffer to disk
    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        flushInternal();
    }

    // Get event count
    size_t getEventCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return totalEvents_;
    }

private:
    AnalyticsService() = default;

    void flushInternal() {
        if (buffer_.empty() || logDir_.empty()) return;

        // Append to daily log file
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        char dateBuf[16];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::localtime(&time));

        fs::path logFile = fs::path(logDir_) / (std::string(dateBuf) + ".jsonl");
        std::ofstream f(logFile, std::ios::app);
        if (f) {
            for (const auto& entry : buffer_) {
                f << entry.dump() << "\n";
            }
            totalEvents_ += buffer_.size();
            spdlog::debug("Flushed {} analytics events to {}", buffer_.size(), logFile.string());
        }
        buffer_.clear();
    }

    mutable std::mutex mutex_;
    bool enabled_ = false;
    std::string logDir_;
    std::vector<nlohmann::json> buffer_;
    size_t totalEvents_ = 0;
};

} // namespace closecrab
