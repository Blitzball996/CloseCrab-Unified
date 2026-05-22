#pragma once
#include <string>
#include <map>
#include <mutex>
#include <chrono>
#include <regex>

namespace closecrab {

struct GitStats {
    int commits = 0;
    int pushes = 0;
    int pulls = 0;
    int branches = 0;
    int prs = 0;
    int merges = 0;
    long long lastOperationTime = 0;
};

class GitTracker {
public:
    static GitTracker& getInstance() {
        static GitTracker instance;
        return instance;
    }

    void trackCommand(const std::string& cmd, int exitCode) {
        if (exitCode != 0) return; // Only track successful operations
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::system_clock::now();
        stats_.lastOperationTime = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        if (std::regex_search(cmd, std::regex("git\\s+commit"))) stats_.commits++;
        else if (std::regex_search(cmd, std::regex("git\\s+push"))) stats_.pushes++;
        else if (std::regex_search(cmd, std::regex("git\\s+pull"))) stats_.pulls++;
        else if (std::regex_search(cmd, std::regex("git\\s+(checkout|switch)\\s+-[bB]"))) stats_.branches++;
        else if (std::regex_search(cmd, std::regex("git\\s+merge"))) stats_.merges++;
        else if (std::regex_search(cmd, std::regex("gh\\s+pr\\s+create"))) stats_.prs++;
    }

    GitStats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    std::string getSummary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string s = "Git operations this session:\n";
        s += "  Commits: " + std::to_string(stats_.commits) + "\n";
        s += "  Pushes: " + std::to_string(stats_.pushes) + "\n";
        s += "  Pulls: " + std::to_string(stats_.pulls) + "\n";
        s += "  Branches: " + std::to_string(stats_.branches) + "\n";
        s += "  PRs: " + std::to_string(stats_.prs) + "\n";
        s += "  Merges: " + std::to_string(stats_.merges) + "\n";
        return s;
    }

private:
    GitTracker() = default;
    mutable std::mutex mutex_;
    GitStats stats_;
};

} // namespace closecrab
