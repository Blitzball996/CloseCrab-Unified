#include <gtest/gtest.h>
#include "../src/api/StreamParser.h"
#include "../src/api/APIError.h"
#include "../src/core/TokenEstimator.h"
#include "../src/core/FileStateCache.h"
#include "../src/core/SessionManager.h"
#include "../src/permissions/PermissionEngine.h"
#include "../src/commands/CommandRegistry.h"
#include "../src/config/Config.h"

#include <filesystem>
#include <fstream>

using namespace closecrab;

// ============================================================
// TokenEstimator tests
// ============================================================

TEST(TokenEstimator, EmptyString) {
    EXPECT_EQ(TokenEstimator::estimate(""), 0);
}

TEST(TokenEstimator, AsciiOnly) {
    // "hello" = 5 chars, 5/4 + 1 = 2
    int tokens = TokenEstimator::estimate("hello");
    EXPECT_GT(tokens, 0);
}

TEST(TokenEstimator, CJKCharacters) {
    // 3 CJK chars (9 bytes UTF-8), should count as ~2 tokens
    std::string cjk = "\xe4\xbd\xa0\xe5\xa5\xbd\xe5\x95\x8a"; // 你好啊
    int tokens = TokenEstimator::estimate(cjk);
    EXPECT_GT(tokens, 0);
}

TEST(TokenEstimator, MixedContent) {
    std::string mixed = "Hello \xe4\xb8\x96\xe7\x95\x8c"; // Hello 世界
    int tokens = TokenEstimator::estimate(mixed);
    EXPECT_GT(tokens, 0);
}

TEST(TokenEstimator, LongString) {
    std::string longStr(10000, 'a');
    int tokens = TokenEstimator::estimate(longStr);
    EXPECT_GT(tokens, 2000); // ~10000/4 = 2500
}

// ============================================================
// PermissionEngine tests
// ============================================================

TEST(PermissionEngine, BypassModeAllowsAll) {
    auto& engine = PermissionEngine::getInstance();
    engine.setMode(PermissionMode::BYPASS);
    auto result = engine.check("Bash", "rm -rf /", false, true);
    EXPECT_EQ(result, PermissionDecision::ALLOWED);
    engine.setMode(PermissionMode::DEFAULT);
}

TEST(PermissionEngine, DefaultModeAllowsReadOnly) {
    auto& engine = PermissionEngine::getInstance();
    engine.setMode(PermissionMode::DEFAULT);
    auto result = engine.check("Read", "read file.txt", true, false);
    EXPECT_EQ(result, PermissionDecision::ALLOWED);
}

TEST(PermissionEngine, DefaultModeAsksForWrite) {
    auto& engine = PermissionEngine::getInstance();
    engine.setMode(PermissionMode::DEFAULT);
    auto result = engine.check("Write", "write file.txt", false, false);
    EXPECT_EQ(result, PermissionDecision::ASK_USER);
}

TEST(PermissionEngine, AutoModeAllowsNonDestructive) {
    auto& engine = PermissionEngine::getInstance();
    engine.setMode(PermissionMode::AUTO);
    auto result = engine.check("Edit", "edit file.txt", false, false);
    EXPECT_EQ(result, PermissionDecision::ALLOWED);
    engine.setMode(PermissionMode::DEFAULT);
}

TEST(PermissionEngine, AutoModeAsksForDestructive) {
    auto& engine = PermissionEngine::getInstance();
    engine.setMode(PermissionMode::AUTO);
    auto result = engine.check("Bash", "rm -rf /", false, true);
    EXPECT_EQ(result, PermissionDecision::ASK_USER);
    engine.setMode(PermissionMode::DEFAULT);
}

TEST(PermissionEngine, DenyRuleOverridesAllow) {
    auto& engine = PermissionEngine::getInstance();
    engine.setMode(PermissionMode::DEFAULT);
    engine.addAllowRule("Bash", "git *");
    engine.addDenyRule("Bash", "git push --force*");
    auto result = engine.check("Bash", "git push --force origin main", false, true);
    EXPECT_EQ(result, PermissionDecision::DENIED);
    engine.removeAllowRule("Bash", "git *");
    engine.removeDenyRule("Bash", "git push --force*");
}

TEST(PermissionEngine, WildcardPatternMatches) {
    auto& engine = PermissionEngine::getInstance();
    engine.addAllowRule("Bash", "npm *");
    auto result = engine.check("Bash", "npm test", false, false);
    EXPECT_EQ(result, PermissionDecision::ALLOWED);
    engine.removeAllowRule("Bash", "npm *");
}

TEST(PermissionEngine, AuditLogRecords) {
    auto& engine = PermissionEngine::getInstance();
    engine.clearAuditLog();
    engine.logDecision("Bash", "ls", PermissionDecision::ALLOWED);
    engine.logDecision("Write", "file.txt", PermissionDecision::ASK_USER);
    auto log = engine.getAuditLog();
    EXPECT_EQ(log.size(), 2);
}

TEST(PermissionEngine, PathAllowedWithNoRestrictions) {
    auto& engine = PermissionEngine::getInstance();
    EXPECT_TRUE(engine.isPathAllowed("/any/path"));
}

TEST(PermissionEngine, PathAllowedWithWorkingDir) {
    auto& engine = PermissionEngine::getInstance();
    engine.addWorkingDirectory("/home/user/project");
    EXPECT_TRUE(engine.isPathAllowed("/home/user/project/src/main.cpp"));
    EXPECT_FALSE(engine.isPathAllowed("/etc/passwd"));
    engine.removeWorkingDirectory("/home/user/project");
}

TEST(PermissionEngine, LoadSaveRules) {
    auto& engine = PermissionEngine::getInstance();
    nlohmann::json rules = {
        {"mode", "auto"},
        {"allow", {{"Bash", {"ls *", "cat *"}}}},
        {"deny", {{"Bash", {"rm *"}}}},
        {"ask", nlohmann::json::object()}
    };
    engine.loadRules(rules);
    EXPECT_EQ(engine.getMode(), PermissionMode::AUTO);

    auto saved = engine.saveRules();
    EXPECT_EQ(saved["mode"], "auto");
    EXPECT_TRUE(saved["allow"]["Bash"].is_array());
    EXPECT_EQ(saved["allow"]["Bash"].size(), 2);

    // Reset
    engine.loadRules({{"mode", "default"}, {"allow", {}}, {"deny", {}}, {"ask", {}}});
}

// ============================================================
// CommandRegistry tests
// ============================================================

TEST(CommandRegistry, IsCommandDetection) {
    EXPECT_TRUE(CommandRegistry::isCommand("/help"));
    EXPECT_TRUE(CommandRegistry::isCommand("/quit"));
    EXPECT_FALSE(CommandRegistry::isCommand("hello"));
    EXPECT_FALSE(CommandRegistry::isCommand(""));
}

TEST(CommandRegistry, ParseCommandNoArgs) {
    auto [name, args] = CommandRegistry::parseCommand("/help");
    EXPECT_EQ(name, "help");
    EXPECT_EQ(args, "");
}

TEST(CommandRegistry, ParseCommandWithArgs) {
    auto [name, args] = CommandRegistry::parseCommand("/commit fix bug");
    EXPECT_EQ(name, "commit");
    EXPECT_EQ(args, "fix bug");
}

TEST(CommandRegistry, ParseCommandExtraSpaces) {
    auto [name, args] = CommandRegistry::parseCommand("/model   gpt-4");
    EXPECT_EQ(name, "model");
    EXPECT_EQ(args, "gpt-4");
}

TEST(CommandRegistry, ParseNonCommand) {
    auto [name, args] = CommandRegistry::parseCommand("hello world");
    EXPECT_EQ(name, "");
    EXPECT_EQ(args, "hello world");
}

// ============================================================
// SessionManager tests
// ============================================================

TEST(SessionManager, CreateAndGetSession) {
    std::string dbPath = "test_sessions.db";
    {
        SessionManager mgr(dbPath);
        std::string id = mgr.createSession("testuser");
        EXPECT_FALSE(id.empty());

        auto session = mgr.getSession(id);
        ASSERT_NE(session, nullptr);
        EXPECT_EQ(session->userId, "testuser");
        EXPECT_EQ(session->context, "{}");
    }
    std::filesystem::remove(dbPath);
}

TEST(SessionManager, UpdateContext) {
    std::string dbPath = "test_sessions_update.db";
    {
        SessionManager mgr(dbPath);
        std::string id = mgr.createSession("user1");
        EXPECT_TRUE(mgr.updateContext(id, "{\"messages\":[]}"));

        auto session = mgr.getSession(id);
        ASSERT_NE(session, nullptr);
        EXPECT_EQ(session->context, "{\"messages\":[]}");
    }
    std::filesystem::remove(dbPath);
}

TEST(SessionManager, DeleteSession) {
    std::string dbPath = "test_sessions_delete.db";
    {
        SessionManager mgr(dbPath);
        std::string id = mgr.createSession("user2");
        EXPECT_TRUE(mgr.deleteSession(id));

        auto session = mgr.getSession(id);
        EXPECT_EQ(session, nullptr);
    }
    std::filesystem::remove(dbPath);
}

TEST(SessionManager, GetNonexistentSession) {
    std::string dbPath = "test_sessions_noexist.db";
    {
        SessionManager mgr(dbPath);
        auto session = mgr.getSession("nonexistent_id");
        EXPECT_EQ(session, nullptr);
    }
    std::filesystem::remove(dbPath);
}

// ============================================================
// Config tests
// ============================================================

TEST(Config, LoadValidYaml) {
    std::string testFile = "test_config.yaml";
    {
        std::ofstream f(testFile);
        f << "provider: local\n"
          << "llm:\n"
          << "  model_path: models/test.gguf\n"
          << "gpu:\n"
          << "  layers: 32\n"
          << "  batch_size: 256\n";
    }

    auto& config = Config::getInstance();
    EXPECT_TRUE(config.load(testFile));
    EXPECT_EQ(config.getString("provider"), "local");
    EXPECT_EQ(config.getString("llm.model_path"), "models/test.gguf");
    EXPECT_EQ(config.getInt("gpu.layers"), 32);
    EXPECT_EQ(config.getInt("gpu.batch_size"), 256);

    std::filesystem::remove(testFile);
}

TEST(Config, MissingKeyReturnsDefault) {
    auto& config = Config::getInstance();
    EXPECT_EQ(config.getString("nonexistent.key", "default"), "default");
    EXPECT_EQ(config.getInt("nonexistent.key", 42), 42);
    EXPECT_EQ(config.getDouble("nonexistent.key", 3.14), 3.14);
}

// ============================================================
// APIError tests (extended)
// ============================================================

TEST(APIError, WithRetrySucceedsOnFirstTry) {
    int attempts = 0;
    withRetry([&]() { attempts++; }, 3);
    EXPECT_EQ(attempts, 1);
}

TEST(APIError, WithRetryRetriesOnRetryableError) {
    int attempts = 0;
    EXPECT_THROW({
        withRetry([&]() {
            attempts++;
            throw APIError(APIErrorType::RATE_LIMIT, 429, "rate limited");
        }, 2);
    }, APIError);
    EXPECT_EQ(attempts, 3); // 1 initial + 2 retries
}

TEST(APIError, WithRetryDoesNotRetryAuthError) {
    int attempts = 0;
    EXPECT_THROW({
        withRetry([&]() {
            attempts++;
            throw APIError(APIErrorType::AUTH_ERROR, 401, "unauthorized");
        }, 3);
    }, APIError);
    EXPECT_EQ(attempts, 1); // No retry for auth errors
}

// ============================================================
// FileStateCache tests (extended)
// ============================================================

TEST(FileStateCache, MissOnNonexistent) {
    auto& cache = FileStateCache::getInstance();
    cache.clear();
    std::string content;
    EXPECT_FALSE(cache.get("nonexistent_file_xyz.txt", content));
}

TEST(FileStateCache, PutAndGetRealFile) {
    // Create a temp file so mtime can be read
    std::string tmpFile = "test_cache_tmp.txt";
    { std::ofstream f(tmpFile); f << "cached content"; }

    auto& cache = FileStateCache::getInstance();
    cache.clear();
    cache.put(tmpFile, "cached content");
    std::string content;
    EXPECT_TRUE(cache.get(tmpFile, content));
    EXPECT_EQ(content, "cached content");

    cache.clear();
    std::filesystem::remove(tmpFile);
}

TEST(FileStateCache, InvalidateRemoves) {
    std::string tmpFile = "test_cache_inv.txt";
    { std::ofstream f(tmpFile); f << "data"; }

    auto& cache = FileStateCache::getInstance();
    cache.clear();
    cache.put(tmpFile, "data");
    cache.invalidate(tmpFile);
    std::string content;
    EXPECT_FALSE(cache.get(tmpFile, content));

    cache.clear();
    std::filesystem::remove(tmpFile);
}

TEST(FileStateCache, ClearRemovesAll) {
    std::string tmpA = "test_cache_a.txt";
    std::string tmpB = "test_cache_b.txt";
    { std::ofstream f(tmpA); f << "aaa"; }
    { std::ofstream f(tmpB); f << "bbb"; }

    auto& cache = FileStateCache::getInstance();
    cache.clear();
    cache.put(tmpA, "aaa");
    cache.put(tmpB, "bbb");
    cache.clear();
    std::string content;
    EXPECT_FALSE(cache.get(tmpA, content));
    EXPECT_FALSE(cache.get(tmpB, content));

    std::filesystem::remove(tmpA);
    std::filesystem::remove(tmpB);
}

// ============================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
