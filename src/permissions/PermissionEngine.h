#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <functional>
#include <nlohmann/json.hpp>

namespace closecrab {

enum class PermissionMode {
    DEFAULT,    // Ask user for each non-read tool use
    AUTO,       // Auto-approve safe, ask for dangerous
    BYPASS      // Allow all
};

enum class PermissionDecision {
    ALLOWED,
    DENIED,
    ASK_USER
};

// Callback for asking user permission
using AskPermissionFn = std::function<bool(const std::string& toolName,
                                            const std::string& description)>;

class PermissionEngine {
public:
    static PermissionEngine& getInstance();

    // Check permission for a tool call
    PermissionDecision check(const std::string& toolName,
                             const std::string& action,
                             bool isReadOnly,
                             bool isDestructive) const;

    // Rule management
    void addAllowRule(const std::string& toolName, const std::string& pattern);
    void addDenyRule(const std::string& toolName, const std::string& pattern);
    void addAskRule(const std::string& toolName, const std::string& pattern);
    void removeAllowRule(const std::string& toolName, const std::string& pattern);
    void removeDenyRule(const std::string& toolName, const std::string& pattern);

    // Mode
    void setMode(PermissionMode mode) { mode_ = mode; }
    PermissionMode getMode() const { return mode_; }
    std::string getModeName() const;

    // User prompt callback
    void setAskCallback(AskPermissionFn fn) { askFn_ = std::move(fn); }

    // Persistence
    void loadRules(const nlohmann::json& j);
    nlohmann::json saveRules() const;

    // Audit log
    void logDecision(const std::string& toolName, const std::string& action,
                     PermissionDecision decision);
    std::vector<std::string> getAuditLog() const;
    void clearAuditLog();

    // ---- v2: Denial tracking ----
    void trackDenial(const std::string& toolName, const std::string& action) {
        std::lock_guard<std::mutex> lock(mutex_);
        denialCounts_[toolName]++;
    }
    int getDenialCount(const std::string& toolName) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = denialCounts_.find(toolName);
        return it != denialCounts_.end() ? it->second : 0;
    }
    std::vector<std::pair<std::string, int>> getDenialSummary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {denialCounts_.begin(), denialCounts_.end()};
    }

    // ---- v2: Multi-working-directory support ----
    void addWorkingDirectory(const std::string& dir) {
        std::lock_guard<std::mutex> lock(mutex_);
        workingDirectories_.push_back(dir);
    }
    void removeWorkingDirectory(const std::string& dir) {
        std::lock_guard<std::mutex> lock(mutex_);
        workingDirectories_.erase(
            std::remove(workingDirectories_.begin(), workingDirectories_.end(), dir),
            workingDirectories_.end());
    }
    bool isPathAllowed(const std::string& path) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (workingDirectories_.empty()) return true; // No restrictions
        for (const auto& dir : workingDirectories_) {
            if (path.find(dir) == 0) return true;
        }
        return false;
    }

    // ---- v2: Rule engine with priority ----
    struct PermissionRule {
        std::string toolName;
        std::string pathPattern;
        PermissionDecision decision;
        int priority = 0;
    };
    void addRule(const PermissionRule& rule) {
        std::lock_guard<std::mutex> lock(mutex_);
        rules_.push_back(rule);
        std::sort(rules_.begin(), rules_.end(),
            [](const PermissionRule& a, const PermissionRule& b) { return a.priority > b.priority; });
    }
    void removeRule(const std::string& toolName, const std::string& pathPattern) {
        std::lock_guard<std::mutex> lock(mutex_);
        rules_.erase(std::remove_if(rules_.begin(), rules_.end(),
            [&](const PermissionRule& r) {
                return r.toolName == toolName && r.pathPattern == pathPattern;
            }), rules_.end());
    }

private:
    PermissionEngine() = default;

    bool matchPattern(const std::string& action, const std::string& pattern) const;
    PermissionDecision matchRules(const std::string& toolName, const std::string& action) const;

    mutable std::mutex mutex_;
    PermissionMode mode_ = PermissionMode::DEFAULT;
    AskPermissionFn askFn_;

    // Rules: toolName -> list of glob patterns
    std::map<std::string, std::vector<std::string>> allowRules_;
    std::map<std::string, std::vector<std::string>> denyRules_;
    std::map<std::string, std::vector<std::string>> askRules_;

    std::vector<std::string> auditLog_;

    // v2 additions
    std::map<std::string, int> denialCounts_;
    std::vector<std::string> workingDirectories_;
    std::vector<PermissionRule> rules_;
};

} // namespace closecrab
