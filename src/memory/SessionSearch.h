#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace closecrab {

struct SearchResult {
    std::string sessionId;
    std::string matchedContent;
    long long timestamp = 0;
    int relevanceScore = 0;
};

class SessionSearch {
public:
    static std::vector<SearchResult> search(const std::string& dbPath,
                                             const std::string& query,
                                             int limit = 10) {
        std::vector<SearchResult> results;
        if (query.empty()) return results;

        sqlite3* db = nullptr;
        int rc = sqlite3_open(dbPath.c_str(), &db);
        if (rc != SQLITE_OK) return results;

        // Search in session context (JSON stored conversations)
        const char* sql = "SELECT id, context, updated_at FROM sessions "
                          "WHERE context LIKE ? ORDER BY updated_at DESC LIMIT ?";
        sqlite3_stmt* stmt;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) { sqlite3_close(db); return results; }

        std::string pattern = "%" + query + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SearchResult r;
            auto col0 = sqlite3_column_text(stmt, 0);
            auto col1 = sqlite3_column_text(stmt, 1);
            r.sessionId = col0 ? reinterpret_cast<const char*>(col0) : "";
            std::string context = col1 ? reinterpret_cast<const char*>(col1) : "";
            r.timestamp = sqlite3_column_int64(stmt, 2);

            // Extract snippet around the match
            r.matchedContent = extractSnippet(context, query);
            r.relevanceScore = countOccurrences(context, query);
            results.push_back(r);
        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);

        // Sort by relevance
        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) {
                return a.relevanceScore > b.relevanceScore;
            });

        return results;
    }

private:
    static std::string extractSnippet(const std::string& text, const std::string& query) {
        size_t pos = text.find(query);
        if (pos == std::string::npos) return "";
        size_t start = (pos > 50) ? pos - 50 : 0;
        size_t end = std::min(pos + query.size() + 50, text.size());
        std::string snippet = text.substr(start, end - start);
        if (start > 0) snippet = "..." + snippet;
        if (end < text.size()) snippet += "...";
        return snippet;
    }

    static int countOccurrences(const std::string& text, const std::string& query) {
        int count = 0;
        size_t pos = 0;
        while ((pos = text.find(query, pos)) != std::string::npos) {
            count++;
            pos += query.size();
        }
        return count;
    }
};

} // namespace closecrab
