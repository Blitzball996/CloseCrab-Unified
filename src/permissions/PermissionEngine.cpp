#include "PermissionEngine.h"
#include "BashClassifier.h"
#include "../utils/PathValidation.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace closecrab {

PermissionEngine& PermissionEngine::getInstance() {
    static PermissionEngine instance;
    return instance;
}

std::string PermissionEngine::getModeName() const {
    switch (mode_) {
        case PermissionMode::DEFAULT: return "default";
        case PermissionMode::AUTO:    return "auto";
        case PermissionMode::BYPASS:  return "bypass";
    }
    return "unknown";
}

bool PermissionEngine::matchPattern(const std::string& action, const std::string& pattern) const {
    if (pattern.empty()) return false;
    if (pattern == "*") return true;

    // Simple wildcard matching: "git *" matches "git push", "git commit", etc.
    if (pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return action.substr(0, prefix.size()) == prefix;
    }

    // Exact match
    return action == pattern;
}

PermissionDecision PermissionEngine::matchRules(const std::string& toolName,
                                                  const std::string& action) const {
    // 1. Check deny rules first (highest priority)
    auto dit = denyRules_.find(toolName);
    if (dit != denyRules_.end()) {
        for (const auto& pattern : dit->second) {
            if (matchPattern(action, pattern)) return PermissionDecision::DENIED;
        }
    }

    // 2. Check allow rules
    auto ait = allowRules_.find(toolName);
    if (ait != allowRules_.end()) {
        for (const auto& pattern : ait->second) {
            if (matchPattern(action, pattern)) return PermissionDecision::ALLOWED;
        }
    }

    // 3. Check ask rules
    auto askit = askRules_.find(toolName);
    if (askit != askRules_.end()) {
        for (const auto& pattern : askit->second) {
            if (matchPattern(action, pattern)) return PermissionDecision::ASK_USER;
        }
    }

    // No rule matched
    return PermissionDecision::ASK_USER;
}

PermissionDecision PermissionEngine::check(const std::string& toolName,
                                            const std::string& action,
                                            bool isReadOnly,
                                            bool isDestructive) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // §5: Dangerous removal path check (JackProAi pathValidation.ts:713-737).
    // Runs BEFORE bypass/allow so a protected-path rm is never auto-approved,
    // even in BYPASS mode or with an allowlist rule. This is the one case the
    // permission system refuses to silence.
    if (toolName == "Bash") {
        auto bracePos = action.find('{');
        if (bracePos != std::string::npos) {
            try {
                auto j = nlohmann::json::parse(action.substr(bracePos));
                std::string cmd = j.value("command", "");
                if (!cmd.empty()) {
                    for (const auto& target : extractRmTargets(cmd)) {
                        std::string resolved = target;
                        if (!resolved.empty() && resolved[0] != '/' &&
                            !(resolved.size() > 1 && resolved[1] == ':')) {
                            // resolve relative against first working dir (or skip)
                            if (!workingDirectories_.empty())
                                resolved = workingDirectories_.front() + "/" + resolved;
                        }
                        if (isDangerousRemovalPath(resolved)) {
                            return PermissionDecision::ASK_USER;
                        }
                    }
                }
            } catch (...) { /* not JSON, fall through */ }
        }
    }

    // Bypass mode: allow everything
    if (mode_ == PermissionMode::BYPASS) return PermissionDecision::ALLOWED;

    // Denial tracking (claude-code pattern): if this tool has been denied 3+
    // times, force ASK_USER regardless of mode — gives the user a chance to
    // approve if the model keeps trying, and signals to the model that it
    // should stop or try a different approach.
    auto denialIt = denialCounts_.find(toolName);
    if (denialIt != denialCounts_.end() && denialIt->second >= 3) {
        return PermissionDecision::ASK_USER;
    }

    // Check explicit rules first
    auto ruleResult = matchRules(toolName, action);
    if (ruleResult == PermissionDecision::ALLOWED) return PermissionDecision::ALLOWED;
    if (ruleResult == PermissionDecision::DENIED) return PermissionDecision::DENIED;

    // Auto mode: allow read-only, ask for destructive, allow rest
    if (mode_ == PermissionMode::AUTO) {
        if (isReadOnly) return PermissionDecision::ALLOWED;
        if (isDestructive) return PermissionDecision::ASK_USER;
        return PermissionDecision::ALLOWED;
    }

    // Default mode: allow read-only, ask for everything else
    if (isReadOnly) return PermissionDecision::ALLOWED;
    return PermissionDecision::ASK_USER;
}

void PermissionEngine::addAllowRule(const std::string& toolName, const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    allowRules_[toolName].push_back(pattern);
}

void PermissionEngine::addDenyRule(const std::string& toolName, const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    denyRules_[toolName].push_back(pattern);
}

void PermissionEngine::addAskRule(const std::string& toolName, const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    askRules_[toolName].push_back(pattern);
}

void PermissionEngine::removeAllowRule(const std::string& toolName, const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = allowRules_.find(toolName);
    if (it != allowRules_.end()) {
        auto& v = it->second;
        v.erase(std::remove(v.begin(), v.end(), pattern), v.end());
    }
}

void PermissionEngine::removeDenyRule(const std::string& toolName, const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = denyRules_.find(toolName);
    if (it != denyRules_.end()) {
        auto& v = it->second;
        v.erase(std::remove(v.begin(), v.end(), pattern), v.end());
    }
}

void PermissionEngine::loadRules(const nlohmann::json& j) {
    std::lock_guard<std::mutex> lock(mutex_);
    allowRules_.clear();
    denyRules_.clear();
    askRules_.clear();

    if (j.contains("allow") && j["allow"].is_object()) {
        for (auto& [tool, patterns] : j["allow"].items()) {
            for (const auto& p : patterns) allowRules_[tool].push_back(p.get<std::string>());
        }
    }
    if (j.contains("deny") && j["deny"].is_object()) {
        for (auto& [tool, patterns] : j["deny"].items()) {
            for (const auto& p : patterns) denyRules_[tool].push_back(p.get<std::string>());
        }
    }
    if (j.contains("ask") && j["ask"].is_object()) {
        for (auto& [tool, patterns] : j["ask"].items()) {
            for (const auto& p : patterns) askRules_[tool].push_back(p.get<std::string>());
        }
    }

    if (j.contains("mode")) {
        std::string m = j["mode"].get<std::string>();
        if (m == "auto") mode_ = PermissionMode::AUTO;
        else if (m == "bypass") mode_ = PermissionMode::BYPASS;
        else mode_ = PermissionMode::DEFAULT;
    }
}

nlohmann::json PermissionEngine::saveRules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json j;
    j["mode"] = getModeName();

    nlohmann::json allow, deny, ask;
    for (const auto& [tool, patterns] : allowRules_) allow[tool] = patterns;
    for (const auto& [tool, patterns] : denyRules_) deny[tool] = patterns;
    for (const auto& [tool, patterns] : askRules_) ask[tool] = patterns;

    j["allow"] = std::move(allow);
    j["deny"] = std::move(deny);
    j["ask"] = std::move(ask);
    return j;
}

void PermissionEngine::logDecision(const std::string& toolName, const std::string& action,
                                    PermissionDecision decision) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");

    std::string decStr;
    switch (decision) {
        case PermissionDecision::ALLOWED: decStr = "ALLOWED"; break;
        case PermissionDecision::DENIED:  decStr = "DENIED"; break;
        case PermissionDecision::ASK_USER: decStr = "ASK"; break;
    }

    auditLog_.push_back("[" + oss.str() + "] " + toolName + " " + action + " -> " + decStr);
}

std::vector<std::string> PermissionEngine::getAuditLog() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return auditLog_;
}

void PermissionEngine::clearAuditLog() {
    std::lock_guard<std::mutex> lock(mutex_);
    auditLog_.clear();
}

} // namespace closecrab
