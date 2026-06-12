#include <gtest/gtest.h>
#include "../src/api/StreamParser.h"
#include "../src/tools/CronTools/CronTools.h"
#include "../src/core/FileStateCache.h"
#include "../src/api/APIError.h"
#include "../src/permissions/PermissionEngine.h"

using namespace closecrab;

// ============================================================
// StreamParser tests
// ============================================================

TEST(StreamParser, ParsesSingleEvent) {
    std::vector<StreamParser::SSEEvent> events;
    StreamParser parser([&](const StreamParser::SSEEvent& e) {
        events.push_back(e);
    });

    parser.feed("event: message\ndata: {\"type\":\"text\"}\n\n");
    parser.finish();

    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event, "message");
    EXPECT_EQ(events[0].data, "{\"type\":\"text\"}");
}

TEST(StreamParser, ParsesMultipleEvents) {
    std::vector<StreamParser::SSEEvent> events;
    StreamParser parser([&](const StreamParser::SSEEvent& e) {
        events.push_back(e);
    });

    parser.feed("data: first\n\ndata: second\n\n");
    parser.finish();

    ASSERT_EQ(events.size(), 2);
    EXPECT_EQ(events[0].data, "first");
    EXPECT_EQ(events[1].data, "second");
}

TEST(StreamParser, HandlesChunkedInput) {
    std::vector<StreamParser::SSEEvent> events;
    StreamParser parser([&](const StreamParser::SSEEvent& e) {
        events.push_back(e);
    });

    parser.feed("data: hel");
    parser.feed("lo world\n\n");
    parser.finish();

    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "hello world");
}

TEST(StreamParser, IgnoresComments) {
    std::vector<StreamParser::SSEEvent> events;
    StreamParser parser([&](const StreamParser::SSEEvent& e) {
        events.push_back(e);
    });

    parser.feed(": this is a comment\ndata: actual\n\n");
    parser.finish();

    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "actual");
}

// ============================================================
// Cron expression parser tests
// ============================================================

TEST(CronParser, ParsesWildcard) {
    auto fields = parseCronExpr("* * * * *");
    EXPECT_TRUE(fields.valid);
    EXPECT_EQ(fields.minutes.size(), 60);
    EXPECT_EQ(fields.hours.size(), 24);
}

TEST(CronParser, ParsesSpecificValues) {
    auto fields = parseCronExpr("30 9 * * *");
    EXPECT_TRUE(fields.valid);
    ASSERT_EQ(fields.minutes.size(), 1);
    EXPECT_EQ(fields.minutes[0], 30);
    ASSERT_EQ(fields.hours.size(), 1);
    EXPECT_EQ(fields.hours[0], 9);
}

TEST(CronParser, ParsesStepValues) {
    auto fields = parseCronExpr("*/5 * * * *");
    EXPECT_TRUE(fields.valid);
    EXPECT_EQ(fields.minutes.size(), 12); // 0,5,10,...,55
    EXPECT_EQ(fields.minutes[0], 0);
    EXPECT_EQ(fields.minutes[1], 5);
}

TEST(CronParser, ParsesRanges) {
    auto fields = parseCronExpr("0 9-17 * * *");
    EXPECT_TRUE(fields.valid);
    EXPECT_EQ(fields.hours.size(), 9); // 9,10,...,17
}

TEST(CronParser, ParsesCommaList) {
    auto fields = parseCronExpr("0 9 * * 1,3,5");
    EXPECT_TRUE(fields.valid);
    ASSERT_EQ(fields.dows.size(), 3);
    EXPECT_EQ(fields.dows[0], 1);
    EXPECT_EQ(fields.dows[1], 3);
    EXPECT_EQ(fields.dows[2], 5);
}

TEST(CronParser, RejectsInvalid) {
    auto fields = parseCronExpr("invalid");
    EXPECT_FALSE(fields.valid);
}

// ============================================================
// APIError classification tests
// ============================================================

TEST(APIError, ClassifiesHttpStatus) {
    EXPECT_EQ(classifyHttpStatus(401), APIErrorType::AUTH_ERROR);
    EXPECT_EQ(classifyHttpStatus(403), APIErrorType::AUTH_ERROR);
    EXPECT_EQ(classifyHttpStatus(429), APIErrorType::RATE_LIMIT);
    EXPECT_EQ(classifyHttpStatus(500), APIErrorType::SERVER_ERROR);
    EXPECT_EQ(classifyHttpStatus(529), APIErrorType::OVERLOADED);
    EXPECT_EQ(classifyHttpStatus(400), APIErrorType::INVALID_REQUEST);
}

TEST(APIError, RetryableCheck) {
    EXPECT_TRUE(isRetryable(APIErrorType::RATE_LIMIT));
    EXPECT_TRUE(isRetryable(APIErrorType::SERVER_ERROR));
    EXPECT_TRUE(isRetryable(APIErrorType::NETWORK_ERROR));
    EXPECT_FALSE(isRetryable(APIErrorType::AUTH_ERROR));
    EXPECT_FALSE(isRetryable(APIErrorType::INVALID_REQUEST));
}

// ============================================================
// FileStateCache tests
// ============================================================

TEST(FileStateCache, MissOnEmpty) {
    auto& cache = FileStateCache::getInstance();
    cache.clear();
    std::string content;
    EXPECT_FALSE(cache.get("nonexistent.txt", content));
}

// ============================================================

// ============================================================
// PermissionEngine wildcard glob matching
// Regression test for the trailing-only matcher bug: wildcard rules
// with leading or mid-pattern '*' silently never matched and fell
// through to ASK_USER. Mirrors upstream Claude Code wildcard fix.
// ============================================================

TEST(PermissionGlob, LeadingWildcardMatchesSubdomains) {
    auto& pe = PermissionEngine::getInstance();
    pe.setMode(PermissionMode::DEFAULT);
    pe.addDenyRule("WebFetch", "*.example.com");
    EXPECT_EQ(pe.check("WebFetch", "api.example.com", true, false),
              PermissionDecision::DENIED);
    EXPECT_EQ(pe.check("WebFetch", "a.b.example.com", true, false),
              PermissionDecision::DENIED);
    EXPECT_NE(pe.check("WebFetch", "example.com.evil.org", true, false),
              PermissionDecision::DENIED);
    pe.removeDenyRule("WebFetch", "*.example.com");
}

TEST(PermissionGlob, MidPatternWildcardMatches) {
    auto& pe = PermissionEngine::getInstance();
    pe.setMode(PermissionMode::DEFAULT);
    pe.addDenyRule("Read", "secrets-*/config.json");
    EXPECT_EQ(pe.check("Read", "secrets-prod/config.json", true, false),
              PermissionDecision::DENIED);
    EXPECT_EQ(pe.check("Read", "secrets-/config.json", true, false),
              PermissionDecision::DENIED);
    EXPECT_NE(pe.check("Read", "secrets-prod/other.json", true, false),
              PermissionDecision::DENIED);
    pe.removeDenyRule("Read", "secrets-*/config.json");
}

TEST(PermissionGlob, MultipleWildcardsAndQuestionMark) {
    auto& pe = PermissionEngine::getInstance();
    pe.setMode(PermissionMode::DEFAULT);
    pe.addDenyRule("WebFetch", "*--force*");
    EXPECT_EQ(pe.check("WebFetch", "git push --force origin", true, false),
              PermissionDecision::DENIED);
    pe.removeDenyRule("WebFetch", "*--force*");

    pe.addDenyRule("Read", "file?.txt");
    EXPECT_EQ(pe.check("Read", "file1.txt", true, false),
              PermissionDecision::DENIED);
    EXPECT_NE(pe.check("Read", "file10.txt", true, false),
              PermissionDecision::DENIED);
    pe.removeDenyRule("Read", "file?.txt");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
