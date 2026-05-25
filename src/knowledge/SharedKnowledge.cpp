#include "SharedKnowledge.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>

namespace closecrab {

static std::string nowISO() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    return buf;
}

SharedKnowledge::SharedKnowledge(const std::string& dbPath) {
    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        spdlog::error("[SharedKnowledge] Failed to open: {}", sqlite3_errmsg(db_));
        db_ = nullptr;
        return;
    }
    initDatabase();
    spdlog::info("[SharedKnowledge] Initialized at {}", dbPath);
}

SharedKnowledge::~SharedKnowledge() {
    if (db_) sqlite3_close(db_);
}

bool SharedKnowledge::initDatabase() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS team_sessions (
            session_id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            participants TEXT,
            history TEXT,
            created_at TEXT,
            last_active_at TEXT
        );
        CREATE TABLE IF NOT EXISTS knowledge (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            question TEXT NOT NULL,
            answer TEXT NOT NULL,
            author TEXT DEFAULT '',
            session_id TEXT DEFAULT '',
            created_at TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_knowledge_author ON knowledge(author);
    )";
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        spdlog::error("[SharedKnowledge] Init failed: {}", err);
        sqlite3_free(err);
        return false;
    }
    return true;
}

std::string SharedKnowledge::createSession(const std::string& name,
                                            const std::vector<std::string>& participants) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sessionId = "team_" + std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    nlohmann::json pArr = participants;
    std::string now = nowISO();

    const char* sql = "INSERT INTO team_sessions (session_id, name, participants, history, created_at, last_active_at) "
                      "VALUES (?, ?, ?, '[]', ?, ?);";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, pArr.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sessionId;
}

bool SharedKnowledge::saveSessionHistory(const std::string& sessionId, const std::string& historyJson) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "UPDATE team_sessions SET history = ?, last_active_at = ? WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, historyJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, nowISO().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::string SharedKnowledge::loadSessionHistory(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "SELECT history FROM team_sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    std::string result = "[]";
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) result = text;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<TeamSession> SharedKnowledge::listSessions(int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TeamSession> sessions;
    const char* sql = "SELECT session_id, name, participants, created_at, last_active_at "
                      "FROM team_sessions ORDER BY last_active_at DESC LIMIT ?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TeamSession s;
        s.sessionId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        s.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* pJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (pJson) {
            try { s.participants = nlohmann::json::parse(pJson).get<std::vector<std::string>>(); } catch (...) {}
        }
        s.createdAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        s.lastActiveAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        sessions.push_back(s);
    }
    sqlite3_finalize(stmt);
    return sessions;
}

bool SharedKnowledge::deleteSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "DELETE FROM team_sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SharedKnowledge::indexQA(const std::string& question, const std::string& answer,
                              const std::string& author, const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "INSERT INTO knowledge (question, answer, author, session_id, created_at) "
                      "VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, question.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, answer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, author.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, nowISO().c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<KnowledgeEntry> SharedKnowledge::search(const std::string& query, int topK) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<KnowledgeEntry> results;
    // SQLite FTS-like search using LIKE (for now; FAISS vector search added when embedding is available)
    const char* sql = "SELECT id, question, answer, author, session_id, created_at "
                      "FROM knowledge WHERE question LIKE ? OR answer LIKE ? "
                      "ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    std::string pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, topK);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        KnowledgeEntry e;
        e.id = sqlite3_column_int(stmt, 0);
        e.question = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        e.answer = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        e.author = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        e.sessionId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        e.createdAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        e.score = 1.0f;
        results.push_back(e);
    }
    sqlite3_finalize(stmt);
    return results;
}

int SharedKnowledge::knowledgeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "SELECT COUNT(*) FROM knowledge;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

} // namespace closecrab
