#pragma once

#include "../Tool.h"
#include <curl/curl.h>
#include <sstream>
#include <map>
#include <mutex>
#include <chrono>

namespace closecrab {

// Simple URL cache with 15-minute TTL
class WebCache {
public:
    static WebCache& getInstance() {
        static WebCache instance;
        return instance;
    }

    bool get(const std::string& url, std::string& content) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(url);
        if (it == cache_.end()) return false;
        auto age = std::chrono::steady_clock::now() - it->second.timestamp;
        if (age > std::chrono::minutes(15)) {
            cache_.erase(it);
            return false;
        }
        content = it->second.content;
        return true;
    }

    void put(const std::string& url, const std::string& content) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Evict old entries if cache is too large
        if (cache_.size() > 100) {
            auto oldest = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.timestamp < oldest->second.timestamp) oldest = it;
            }
            cache_.erase(oldest);
        }
        cache_[url] = {content, std::chrono::steady_clock::now()};
    }

private:
    WebCache() = default;
    struct Entry {
        std::string content;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::mutex mutex_;
    std::map<std::string, Entry> cache_;
};

class WebSearchTool : public Tool {
public:
    std::string getName() const override { return "WebSearch"; }
    std::string getDescription() const override {
        return "Search the web and return results.";
    }
    std::string getCategory() const override { return "internet"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Search query"}}}
            }},
            {"required", {"query"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string query = input["query"].get<std::string>();

        // URL encode
        CURL* curl = curl_easy_init();
        if (!curl) return ToolResult::fail("CURL init failed");

        char* encoded = curl_easy_escape(curl, query.c_str(), (int)query.size());
        std::string url = "https://api.duckduckgo.com/?q=" + std::string(encoded) + "&format=json&no_html=1";
        curl_free(encoded);

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "CloseCrab-Unified/0.1");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return ToolResult::fail(std::string("Search failed: ") + curl_easy_strerror(res));
        }

        // Parse DuckDuckGo response
        try {
            auto j = nlohmann::json::parse(response);
            std::string result;

            if (j.contains("Abstract") && !j["Abstract"].get<std::string>().empty()) {
                result += j["Abstract"].get<std::string>() + "\n\n";
            }
            if (j.contains("RelatedTopics") && j["RelatedTopics"].is_array()) {
                int count = 0;
                for (const auto& topic : j["RelatedTopics"]) {
                    if (topic.contains("Text") && count < 5) {
                        result += "- " + topic["Text"].get<std::string>() + "\n";
                        count++;
                    }
                }
            }
            if (result.empty()) result = "No results found for: " + query;
            return ToolResult::ok(result);
        } catch (...) {
            return ToolResult::ok(response.substr(0, 2000));
        }
    }

private:
    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* str = static_cast<std::string*>(userdata);
        str->append(ptr, size * nmemb);
        return size * nmemb;
    }
};

class WebFetchTool : public Tool {
public:
    std::string getName() const override { return "WebFetch"; }
    std::string getDescription() const override {
        return "Fetch content from a URL and return it as text.";
    }
    std::string getCategory() const override { return "internet"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"url", {{"type", "string"}, {"description", "URL to fetch"}}},
                {"prompt", {{"type", "string"}, {"description", "What to extract from the page"}}}
            }},
            {"required", {"url"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string url = input["url"].get<std::string>();

        // Check cache first
        std::string cached;
        if (WebCache::getInstance().get(url, cached)) {
            return ToolResult::ok(cached);
        }

        CURL* curl = curl_easy_init();
        if (!curl) return ToolResult::fail("CURL init failed");

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "CloseCrab-Unified/0.1");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return ToolResult::fail(std::string("Fetch failed: ") + curl_easy_strerror(res));
        }

        // Truncate large responses
        if (response.size() > 50000) {
            response = response.substr(0, 50000) + "\n... (truncated)";
        }

        // Cache the result
        WebCache::getInstance().put(url, response);

        return ToolResult::ok(response);
    }

private:
    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* str = static_cast<std::string*>(userdata);
        str->append(ptr, size * nmemb);
        return size * nmemb;
    }
};

} // namespace closecrab
