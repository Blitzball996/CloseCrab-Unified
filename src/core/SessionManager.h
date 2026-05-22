#pragma once
#include <string>
#include <memory>
#include <vector>
#include <sqlite3.h>
#include <filesystem>
#include <spdlog/spdlog.h>

struct Session {
    std::string id;
    std::string userId;
    std::string context;  // JSON ��ʽ�ĶԻ�������
    long long createdAt;
    long long updatedAt;
};

class SessionManager {
public:
    SessionManager(const std::string& dbPath);
    ~SessionManager();

    // �����»Ự
    std::string createSession(const std::string& userId);

    // ��ȡ�Ự
    std::shared_ptr<Session> getSession(const std::string& sessionId);

    // ���»Ự������
    bool updateContext(const std::string& sessionId, const std::string& context);

    // ɾ���Ự
    bool deleteSession(const std::string& sessionId);

    // 列出最近的会话
    std::vector<std::shared_ptr<Session>> listSessions(int limit = 10);

private:
    sqlite3* db;
    bool initDatabase();
};