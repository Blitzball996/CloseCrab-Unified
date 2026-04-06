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
    // ��¼����
    std::string logEntry = "Skill: " + skillName + ", Action: " + action;
    log(logEntry);

    // ���Ȩ��
    if (!checkPermission(skillName, action, level)) {
        std::string msg = "Ȩ�޲���: " + skillName + " - " + action;
        log(msg);
        return "[��ȫɳ��] " + msg;
    }

    // ִ��
    try {
        std::string result = executor();
        log("ִ�гɹ�: " + skillName + " -> " + result.substr(0, 100));
        return result;
    }
    catch (const std::exception& e) {
        log("ִ��ʧ��: " + skillName + " - " + e.what());
        return "[��ȫɳ��] ִ��ʧ��: " + std::string(e.what());
    }
}

bool Sandbox::checkPermission(const std::string& skill,
    const std::string& action,
    PermissionLevel level) {
    // ��������
    if (isBlacklisted(skill, action)) {
        log("����������: " + skill + " - " + action);
        return false;
    }

    // ��������
    if (isWhitelisted(skill, action)) {
        log("������ͨ��: " + skill + " - " + action);
        return true;
    }

    // ����ģʽ�ж�
    switch (currentMode) {
    case Mode::DISABLED:
        log("ɳ����ã�ֱ��ִ��: " + skill + " - " + action);
        return true;

    case Mode::TRUSTED:
        log("����ģʽ������ִ��: " + skill + " - " + action);
        return true;

    case Mode::AUTO:
        if (level == PermissionLevel::SAFE) {
            return true;
        }
        else if (level == PermissionLevel::NORMAL) {
            log("�Զ�������ͨ����: " + skill + " - " + action);
            return true;
        }
        else {
            log("�Զ��ܾ�Σ�ղ���: " + skill + " - " + action);
            return false;
        }

    case Mode::ASK:
        if (permissionCallback) {
            return permissionCallback(skill, action, level);
        }
        else {
            // û�лص�ʱ��Σ�ղ���Ĭ�Ͼܾ�
            if (level >= PermissionLevel::DANGEROUS) {
                std::cout << "\n[��ȫɳ��] " << skill << " ��Ҫִ��: " << action;
                std::cout << "\n�Ƿ�����? (y/n): ";
                std::string answer;
                std::getline(std::cin, answer);
                return (answer == "y" || answer == "Y");
            }
            return true;
        }

    default:
        return false;
    }
}

void Sandbox::addWhitelist(const std::string& skill, const std::string& action) {
    whitelist.emplace_back(skill, action);
    spdlog::info("Added to whitelist: {} - {}", skill, action);
}

void Sandbox::addBlacklist(const std::string& skill, const std::string& action) {
    blacklist.emplace_back(skill, action);
    spdlog::info("Added to blacklist: {} - {}", skill, action);
}

std::vector<std::string> Sandbox::getAuditLog() const {
    return auditLog;
}

void Sandbox::clearAuditLog() {
    auditLog.clear();
    spdlog::info("Audit log cleared");
}

void Sandbox::log(const std::string& entry) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << " | " << entry;
    auditLog.push_back(ss.str());
    spdlog::debug("Sandbox: {}", entry);
}

bool Sandbox::isWhitelisted(const std::string& skill, const std::string& action) const {
    for (const auto& item : whitelist) {
        if (item.first == skill && (item.second == "*" || item.second == action)) {
            return true;
        }
    }
    return false;
}

bool Sandbox::isBlacklisted(const std::string& skill, const std::string& action) const {
    for (const auto& item : blacklist) {
        if (item.first == skill && (item.second == "*" || item.second == action)) {
            return true;
        }
    }
    return false;
}