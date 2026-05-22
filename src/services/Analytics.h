#pragma once
#include <string>
#include <map>
#include <mutex>
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>

namespace closecrab {

class Analytics {
public:
    static Analytics& getInstance() {
        static Analytics instance;
        return instance;
    }

    void trackToolUse(const std::string& toolName) {
        std::lock_guard<std::mutex> lock(mutex_);
        toolUsage_[toolName]++;
        totalToolCalls_++;
    }

    void trackCommand(const std::string& cmdName) {
        std::lock_guard<std::mutex> lock(mutex_);
        commandUsage_[cmdName]++;
    }

    void trackTokens(int input, int output) {
        std::lock_guard<std::mutex> lock(mutex_);
        totalInputTokens_ += input;
        totalOutputTokens_ += output;
    }

    void trackSession() {
        std::lock_guard<std::mutex> lock(mutex_);
        sessionCount_++;
    }

    std::string getSummary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string s = "Usage Analytics (this session):\n";
        s += "  Total tool calls: " + std::to_string(totalToolCalls_) + "\n";
        s += "  Total tokens: " + std::to_string(totalInputTokens_) + " in / "
             + std::to_string(totalOutputTokens_) + " out\n";
        s += "\n  Top tools:\n";

        // Sort by usage count
        std::vector<std::pair<std::string, int>> sorted(toolUsage_.begin(), toolUsage_.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < std::min(sorted.size(), (size_t)10); i++) {
            s += "    " + sorted[i].first + ": " + std::to_string(sorted[i].second) + "\n";
        }

        if (!commandUsage_.empty()) {
            s += "\n  Top commands:\n";
            std::vector<std::pair<std::string, int>> cmdSorted(commandUsage_.begin(), commandUsage_.end());
            std::sort(cmdSorted.begin(), cmdSorted.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            for (size_t i = 0; i < std::min(cmdSorted.size(), (size_t)5); i++) {
                s += "    /" + cmdSorted[i].first + ": " + std::to_string(cmdSorted[i].second) + "\n";
            }
        }
        return s;
    }

    void saveToFile(const std::string& path = "data/analytics.json") const {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json j;
        j["tool_usage"] = toolUsage_;
        j["command_usage"] = commandUsage_;
        j["total_tool_calls"] = totalToolCalls_;
        j["total_input_tokens"] = totalInputTokens_;
        j["total_output_tokens"] = totalOutputTokens_;
        j["session_count"] = sessionCount_;
        j["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::ofstream f(path);
        if (f.is_open()) f << j.dump(2);
    }

private:
    Analytics() = default;
    mutable std::mutex mutex_;
    std::map<std::string, int> toolUsage_;
    std::map<std::string, int> commandUsage_;
    int totalToolCalls_ = 0;
    long long totalInputTokens_ = 0;
    long long totalOutputTokens_ = 0;
    int sessionCount_ = 0;
};

} // namespace closecrab
