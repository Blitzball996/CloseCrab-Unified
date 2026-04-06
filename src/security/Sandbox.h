#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>

// PermissionLevel (originally in Skill.h, moved here for standalone use)
enum class PermissionLevel {
    SAFE = 0,
    NORMAL = 1,
    DANGEROUS = 2,
    UNSAFE = 3
};

// Ȩ������ص�
using PermissionCallback = std::function<bool(const std::string& skill,
    const std::string& action,
    PermissionLevel level)>;

class Sandbox {
public:
    static Sandbox& getInstance();

    // ���ð�ȫģʽ
    enum class Mode {
        DISABLED = 0,   // ��ȫ����ɳ�䣨ֱ��ִ�У�
        ASK = 1,        // ÿ��ѯ���û�
        AUTO = 2,       // �Զ��ܾ�Σ�ղ���
        TRUSTED = 3     // ����ģʽ��ֻ��¼�������أ�
    };

    void setMode(Mode mode);
    Mode getMode() const;
    void setPermissionCallback(PermissionCallback callback);

    std::string executeSkill(const std::string& skillName,
        const std::string& action,
        PermissionLevel level,
        std::function<std::string()> executor);

    void addWhitelist(const std::string& skill, const std::string& action);
    void addBlacklist(const std::string& skill, const std::string& action);
    std::vector<std::string> getAuditLog() const;
    void clearAuditLog();

private:
    Sandbox() = default;
    Mode currentMode = Mode::ASK;
    PermissionCallback permissionCallback;
    std::vector<std::pair<std::string, std::string>> whitelist;
    std::vector<std::pair<std::string, std::string>> blacklist;
    std::vector<std::string> auditLog;

    void log(const std::string& entry);
    bool checkPermission(const std::string& skill,
        const std::string& action,
        PermissionLevel level);
    bool isWhitelisted(const std::string& skill, const std::string& action) const;
    bool isBlacklisted(const std::string& skill, const std::string& action) const;
};