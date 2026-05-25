#include "StatsEngine.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace closecrab {

StatsEngine::StatsEngine(const std::string& dbPath) {
    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        spdlog::error("[StatsEngine] Failed to open database: {}", sqlite3_errmsg(db_));
        db_ = nullptr;
        return;
    }
    initDatabase();
}

StatsEngine::~StatsEngine() {
    if (db_) sqlite3_close(db_);
}

bool StatsEngine::initDatabase() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS user_stats (
            client_id TEXT PRIMARY KEY,
            username TEXT NOT NULL,
            total_messages INTEGER DEFAULT 0,
            tools_used INTEGER DEFAULT 0,
            bugs_fixed INTEGER DEFAULT 0,
            lines_written INTEGER DEFAULT 0,
            tests_run INTEGER DEFAULT 0,
            session_minutes INTEGER DEFAULT 0,
            current_streak INTEGER DEFAULT 0,
            last_active_date TEXT DEFAULT '',
            created_at INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS achievements (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            client_id TEXT NOT NULL,
            achievement TEXT NOT NULL,
            earned_at INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_stats_username ON user_stats(username);
    )";
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        spdlog::error("[StatsEngine] DB init failed: {}", err);
        sqlite3_free(err);
        return false;
    }
    return true;
}

void StatsEngine::recordAction(const std::string& clientId, const std::string& username,
                                ActionType type, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;

    const char* upsert = R"(
        INSERT INTO user_stats (client_id, username, created_at)
        VALUES (?, ?, ?)
        ON CONFLICT(client_id) DO NOTHING;
    )";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, upsert, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, clientId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, std::chrono::system_clock::now().time_since_epoch().count());
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    std::string column;
    switch (type) {
        case ActionType::MESSAGE_SENT: column = "total_messages"; break;
        case ActionType::TOOL_USED:    column = "tools_used"; break;
        case ActionType::BUG_FIXED:    column = "bugs_fixed"; break;
        case ActionType::CODE_WRITTEN: column = "lines_written"; break;
        case ActionType::TEST_PASSED:  column = "tests_run"; break;
        case ActionType::SESSION_TIME: column = "session_minutes"; break;
    }

    std::string update = "UPDATE user_stats SET " + column + " = " + column + " + ? WHERE client_id = ?;";
    sqlite3_prepare_v2(db_, update.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, value);
    sqlite3_bind_text(stmt, 2, clientId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

UserStats StatsEngine::getStats(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    UserStats stats;
    if (!db_) return stats;

    const char* sql = "SELECT username, total_messages, tools_used, bugs_fixed, "
                      "lines_written, tests_run, session_minutes, current_streak "
                      "FROM user_stats WHERE client_id = ?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, clientId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        stats.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        stats.totalMessages = sqlite3_column_int(stmt, 1);
        stats.toolsUsed = sqlite3_column_int(stmt, 2);
        stats.bugsFixed = sqlite3_column_int(stmt, 3);
        stats.linesWritten = sqlite3_column_int(stmt, 4);
        stats.testsRun = sqlite3_column_int(stmt, 5);
        stats.sessionMinutes = sqlite3_column_int(stmt, 6);
        stats.currentStreak = sqlite3_column_int(stmt, 7);
        stats.totalScore = computeScore(stats);
    }
    sqlite3_finalize(stmt);
    return stats;
}

std::vector<LeaderboardEntry> StatsEngine::getLeaderboard(int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LeaderboardEntry> board;
    if (!db_) return board;

    const char* sql = "SELECT client_id, username, total_messages, tools_used, "
                      "bugs_fixed, lines_written, tests_run "
                      "FROM user_stats ORDER BY "
                      "(total_messages * 1 + tools_used * 2 + bugs_fixed * 10 + "
                      "lines_written * 1 + tests_run * 5) DESC LIMIT ?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);

    int rank = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LeaderboardEntry entry;
        entry.clientId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        entry.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        UserStats s;
        s.totalMessages = sqlite3_column_int(stmt, 2);
        s.toolsUsed = sqlite3_column_int(stmt, 3);
        s.bugsFixed = sqlite3_column_int(stmt, 4);
        s.linesWritten = sqlite3_column_int(stmt, 5);
        s.testsRun = sqlite3_column_int(stmt, 6);
        entry.score = computeScore(s);
        entry.rank = rank++;
        board.push_back(entry);
    }
    sqlite3_finalize(stmt);
    return board;
}

std::vector<std::string> StatsEngine::checkNewAchievements(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> newAchievements;
    if (!db_) return newAchievements;

    auto stats = getStats(clientId);
    std::vector<std::pair<std::string, bool>> checks = {
        {"First Message", stats.totalMessages >= 1},
        {"Tool Master", stats.toolsUsed >= 50},
        {"Bug Slayer", stats.bugsFixed >= 10},
        {"Code Machine", stats.linesWritten >= 1000},
        {"Test Driven", stats.testsRun >= 20},
        {"Marathon Coder", stats.sessionMinutes >= 480},
        {"Streak 7", stats.currentStreak >= 7},
        {"Centurion", stats.totalMessages >= 100},
        {"10K Lines", stats.linesWritten >= 10000},
    };

    for (auto& [name, earned] : checks) {
        if (!earned) continue;
        const char* checkSql = "SELECT COUNT(*) FROM achievements WHERE client_id = ? AND achievement = ?;";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, checkSql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, clientId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        if (count == 0) {
            const char* insertSql = "INSERT INTO achievements (client_id, achievement, earned_at) VALUES (?, ?, ?);";
            sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr);
            sqlite3_bind_text(stmt, 1, clientId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, std::chrono::system_clock::now().time_since_epoch().count());
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            newAchievements.push_back(name);
        }
    }
    return newAchievements;
}

int StatsEngine::computeScore(const UserStats& stats) {
    return stats.totalMessages * 1
         + stats.toolsUsed * 2
         + stats.bugsFixed * 10
         + stats.linesWritten * 1
         + stats.testsRun * 5;
}

} // namespace closecrab
