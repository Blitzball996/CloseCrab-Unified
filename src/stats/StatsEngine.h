#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <sqlite3.h>

namespace closecrab {

enum class ActionType {
    MESSAGE_SENT,
    TOOL_USED,
    BUG_FIXED,
    CODE_WRITTEN,
    TEST_PASSED,
    SESSION_TIME,
};

struct UserStats {
    std::string username;
    int totalMessages = 0;
    int toolsUsed = 0;
    int bugsFixed = 0;
    int linesWritten = 0;
    int testsRun = 0;
    int sessionMinutes = 0;
    int currentStreak = 0;
    int totalScore = 0;
    std::vector<std::string> achievements;
};

struct LeaderboardEntry {
    std::string username;
    std::string clientId;
    int score;
    int rank;
};

class StatsEngine {
public:
    explicit StatsEngine(const std::string& dbPath);
    ~StatsEngine();

    void recordAction(const std::string& clientId, const std::string& username, ActionType type, int value = 1);
    UserStats getStats(const std::string& clientId);
    std::vector<LeaderboardEntry> getLeaderboard(int limit = 10);
    std::vector<std::string> checkNewAchievements(const std::string& clientId);

private:
    bool initDatabase();
    int computeScore(const UserStats& stats);

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

} // namespace closecrab
