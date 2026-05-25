#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>

namespace closecrab {

struct TeamSession {
    std::string sessionId;
    std::string name;
    std::vector<std::string> participants;
    std::string createdAt;
    std::string lastActiveAt;
};

struct KnowledgeEntry {
    int id;
    std::string question;
    std::string answer;
    std::string author;
    std::string sessionId;
    std::string createdAt;
    float score;
};

class SharedKnowledge {
public:
    explicit SharedKnowledge(const std::string& dbPath);
    ~SharedKnowledge();

    // Session management: save/restore collaboration sessions
    std::string createSession(const std::string& name, const std::vector<std::string>& participants);
    bool saveSessionHistory(const std::string& sessionId, const std::string& historyJson);
    std::string loadSessionHistory(const std::string& sessionId);
    std::vector<TeamSession> listSessions(int limit = 20);
    bool deleteSession(const std::string& sessionId);

    // Knowledge base: index Q&A pairs for team search
    bool indexQA(const std::string& question, const std::string& answer,
                 const std::string& author, const std::string& sessionId = "");
    std::vector<KnowledgeEntry> search(const std::string& query, int topK = 5);
    int knowledgeCount() const;

private:
    bool initDatabase();
    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace closecrab
