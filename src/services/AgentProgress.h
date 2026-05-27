#pragma once
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>

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

// Thread-safe sink for streaming sub-agent activity to the main UI.
// AgentManager (running on worker threads) emits one line per sub-agent tool
// event; main.cpp installs a sink that prints them with an [agentId] prefix so
// the user can see what each sub-agent is doing instead of an opaque spinner.
class AgentActivitySink {
public:
    static AgentActivitySink& getInstance() {
        static AgentActivitySink instance;
        return instance;
    }

    using Handler = std::function<void(const std::string& agentId, const std::string& line)>;

    void setHandler(Handler h) {
        std::lock_guard<std::mutex> lock(mutex_);
        handler_ = std::move(h);
    }

    void log(const std::string& agentId, const std::string& line) {
        Handler h;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            h = handler_;
        }
        if (h) h(agentId, line);
    }

private:
    AgentActivitySink() = default;
    std::mutex mutex_;
    Handler handler_;
};

} // namespace closecrab
