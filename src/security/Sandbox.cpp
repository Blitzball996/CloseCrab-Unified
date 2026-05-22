#include "Sandbox.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

Sandbox& Sandbox::getInstance() {
    static Sandbox instance;
    return instance;
}

void Sandbox::setMode(Mode mode) {
    currentMode = mode;
    std::string modeStr;
    switch (mode) {
    case Mode::DISABLED: modeStr = "DISABLED"; break;
    case Mode::ASK: modeStr = "ASK"; break;
    case Mode::AUTO: modeStr = "AUTO"; break;
    case Mode::TRUSTED: modeStr = "TRUSTED"; break;
    }
    spdlog::info("Sandbox mode set to: {}", modeStr);
}

Sandbox::Mode Sandbox::getMode() const {
    return currentMode;
}

void Sandbox::setPermissionCallback(PermissionCallback callback) {
    permissionCallback = callback;
}

std::string Sandbox::executeSkill(const std::string& skillName,
    const std::string& action,
    PermissionLevel level,
    std::function<std::string()> executor) {
    std::string logEntry = "Skill: " + skillName + ", Action: " + action;
    log(logEntry);

    if (!checkPermission(skillName, action, level)) {
        std::string msg = "Permission denied: " + skillName + " - " + action;
        log(msg);
        return "[Sandbox] " + msg;
    }

    try {
        std::string result = executor();
        log("Success: " + skillName + " -> " + result.substr(0, 100));
        return result;
    }
    catch (const std::exception& e) {
        log("Failed: " + skillName + " - " + e.what());
        return "[Sandbox] Execution failed: " + std::string(e.what());
    }
}

bool Sandbox::checkPermission(const std::string& skill,
    const std::string& action,
    PermissionLevel level) {
    if (isBlacklisted(skill, action)) {
        log("Blacklisted: " + skill + " - " + action);
        return false;
    }

    if (isWhitelisted(skill, action)) {
        return true;
    }

    switch (currentMode) {
    case Mode::DISABLED:
        return true;
    case Mode::TRUSTED:
        return level <= PermissionLevel::DANGEROUS;
    case Mode::AUTO:
        return level <= PermissionLevel::NORMAL;
    case Mode::ASK:
        if (permissionCallback) {
            return permissionCallback(skill, action, level);
        }
        return false;
    }
    return false;
}

bool Sandbox::isBlacklisted(const std::string& skill, const std::string& action) const {
    for (const auto& entry : blacklist) {
        if (entry.first == skill && (entry.second.empty() || entry.second == action)) return true;
    }
    return false;
}

bool Sandbox::isWhitelisted(const std::string& skill, const std::string& action) const {
    for (const auto& entry : whitelist) {
        if (entry.first == skill && (entry.second.empty() || entry.second == action)) return true;
    }
    return false;
}

void Sandbox::addBlacklist(const std::string& skill, const std::string& action) {
    blacklist.push_back({skill, action});
}

void Sandbox::addWhitelist(const std::string& skill, const std::string& action) {
    whitelist.push_back({skill, action});
}

void Sandbox::log(const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%H:%M:%S");
    auditLog.push_back("[" + oss.str() + "] " + msg);
    spdlog::debug("[Sandbox] {}", msg);
}

std::vector<std::string> Sandbox::getAuditLog() const {
    return auditLog;
}

void Sandbox::clearAuditLog() {
    auditLog.clear();
}
