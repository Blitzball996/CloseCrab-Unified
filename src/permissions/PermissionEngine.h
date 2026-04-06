#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
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
};

} // namespace closecrab
