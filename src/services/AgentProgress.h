#pragma once
#include <string>
#include <map>
#include <mutex>
#include <atomic>

namespace closecrab {

class AgentProgress {
public:
    static AgentProgress& getInstance() {
        static AgentProgress instance;
        return instance;
    }

    void setProgress(const std::string& agentId, int percent, const std::string& action = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_[agentId] = percent;
        actions_[agentId] = action;
    }

    int getProgress(const std::string& agentId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = progress_.find(agentId);
        return it != progress_.end() ? it->second : 0;
    }

    std::string getAction(const std::string& agentId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = actions_.find(agentId);
        return it != actions_.end() ? it->second : "";
    }

    std::string getDisplayString(const std::string& agentId) const {
        int pct = getProgress(agentId);
        std::string action = getAction(agentId);
        std::string s = "[" + std::to_string(pct) + "%]";
        if (!action.empty()) s += " " + action;
        return s;
    }

    void remove(const std::string& agentId) {
        std::lock_guard<std::mutex> lock(mutex_);
        progress_.erase(agentId);
        actions_.erase(agentId);
    }

private:
    AgentProgress() = default;
    mutable std::mutex mutex_;
    std::map<std::string, int> progress_;
    std::map<std::string, std::string> actions_;
};

} // namespace closecrab
