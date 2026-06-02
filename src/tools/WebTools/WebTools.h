#pragma once

#include "../Tool.h"
#include "../../config/Config.h"
#include "../../utils/ProxyConfig.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <map>
#include <mutex>
#include <chrono>
#include <thread>
#include <string>
#include <cstdlib>
#include <atomic>
#include <utility>
#include <vector>

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

// ---------------------------------------------------------------------------
// WebSearchTool — server-side web search (JackProAi / Claude Code approach).
//
// The OLD implementation curl'd api.duckduckgo.com directly: (1) it didn't go
// through the proxy, so behind the GFW it timed out ("Search failed: Timeout
// was reached"), and (2) DuckDuckGo's Instant-Answer API returns almost nothing
// for normal queries.
//
// NEW: we mirror Claude Code — issue a one-shot Anthropic API request carrying
// the SERVER-SIDE tool `web_search_20250305`. The search runs on the
// Anthropic/relay side (verified working on yikoulian.cc), so it travels the
// SAME already-working API link as the main conversation, with the same proxy.
// We parse the returned `web_search_tool_result` (title/url) + text blocks.
// ---------------------------------------------------------------------------
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
        std::string query = input.value("query", "");
        if (query.empty()) return ToolResult::fail("WebSearch: empty query");

        auto& cfg = Config::getInstance();
        std::string provider = cfg.getString("provider", "anthropic");
        std::string apiKey   = cfg.getString("api.api_key", "");
        std::string baseUrl  = cfg.getString("api.base_url", "https://api.anthropic.com");
        std::string mainModel = cfg.getString("api.model", "claude-sonnet-4-20250514");
        std::string fallback  = cfg.getString("api.fallback_model", "claude-sonnet-4-20250514");

        // Run the search on the SMALL/FAST model (fallback, usually sonnet), NOT the
        // main model (usually opus). Rationale (matches JackProAi's getSmallFastModel
        // path for web_search): the relay's opus capacity is scarce — doing the search
        // on opus AND then the main loop on opus is a "double-opus" burst that hits the
        // relay's intermittent "所有供应商暂时不可用" (503) overload window. Sonnet has
        // ample capacity (measured: always 200), so it both frees opus for the main
        // turn and is reliably available. Override with CLOSECRAB_WEBSEARCH_MODEL.
        std::string model = fallback.empty() ? mainModel : fallback;
        if (const char* e = std::getenv("CLOSECRAB_WEBSEARCH_MODEL")) { if (e[0]) model = e; }

        // Env overrides (same priority as main.cpp).
        if (apiKey.empty()) { if (const char* e = std::getenv("ANTHROPIC_AUTH_TOKEN")) apiKey = e; }
        if (const char* e = std::getenv("ANTHROPIC_BASE_URL")) { if (e[0]) baseUrl = e; }

        // Server-side web search needs the hosted API. In local-LLM mode there is
        // no Anthropic endpoint to run the search — tell the user how to enable it.
        if (provider == "local" || apiKey.empty()) {
            return ToolResult::fail(
                "WebSearch needs the hosted API (provider: anthropic). "
                "Set it with /api provider=anthropic and /api key=sk-... , then restart.");
        }

        // Build the one-shot request with the web_search server tool.
        nlohmann::json body = {
            {"model", model},
            {"max_tokens", 2048},
            {"stream", false},
            {"messages", nlohmann::json::array({
                {{"role", "user"},
                 {"content", "Perform a web search and answer concisely with sources: " + query}}
            })},
            {"tools", nlohmann::json::array({
                {{"type", "web_search_20250305"}, {"name", "web_search"}, {"max_uses", 5}}
            })}
        };
        std::string bodyStr = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

        // Strip trailing slashes from base URL to avoid `//v1`.
        while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
        std::string url = baseUrl + "/v1/messages";

        // Retry loop: transient relay errors (503 "所有供应商暂时不可用", 429, 5xx,
        // network blips) get exponential-backoff retries — matching JackProAi's
        // withRetry on its web_search sub-query (previously this had NO retry, so a
        // single transient 503 killed the whole search).
        std::string response;
        long httpCode = 0;
        CURLcode res = CURLE_OK;
        constexpr int kMaxAttempts = 4;
        for (int attempt = 1; attempt <= kMaxAttempts; attempt++) {
            if (ctx.abortFlag && ctx.abortFlag->load()) return ToolResult::fail("Search aborted");
            response.clear();
            httpCode = 0;

            CURL* curl = curl_easy_init();
            if (!curl) return ToolResult::fail("CURL init failed");

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, ("x-api-key: " + apiKey).c_str());
            headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
            headers = curl_slist_append(headers, "anthropic-beta: web-search-2025-03-05");
            headers = curl_slist_append(headers, "content-type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)bodyStr.size());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);        // search can take a while
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
            applyProxyToCurl(curl);                                // go through the proxy
            if (ctx.abortFlag) {
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void*)ctx.abortFlag);
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
                    +[](void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                        auto* f = static_cast<const std::atomic<bool>*>(p);
                        return (f && f->load()) ? 1 : 0;
                    });
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            }

            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            // Retryable: network error OR 429/5xx. Non-retryable (4xx auth/bad-request): stop.
            bool transient = (res != CURLE_OK) || httpCode == 429 || httpCode >= 500;
            if (!transient || attempt == kMaxAttempts) break;
            // Exponential backoff: 600ms, 1.2s, 2.4s
            int delayMs = 600 * (1 << (attempt - 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }

        if (res != CURLE_OK) {
            return ToolResult::fail(std::string("Search failed: ") + curl_easy_strerror(res) +
                                    " (proxy=" + (resolveProxyUrl().empty() ? "none" : resolveProxyUrl()) + ")");
        }
        if (httpCode >= 400) {
            std::string msg = response.substr(0, 300);
            try { auto j = nlohmann::json::parse(response);
                  if (j.contains("error") && j["error"].contains("message"))
                      msg = j["error"]["message"].get<std::string>(); } catch (...) {}
            return ToolResult::fail("Search HTTP " + std::to_string(httpCode) + ": " + msg);
        }

        // Parse content blocks: collect text + (title,url) sources.
        try {
            auto j = nlohmann::json::parse(response);
            std::string text;
            std::vector<std::pair<std::string,std::string>> sources;
            if (j.contains("content") && j["content"].is_array()) {
                for (const auto& block : j["content"]) {
                    std::string t = block.value("type", "");
                    if (t == "text") {
                        text += block.value("text", "");
                    } else if (t == "web_search_tool_result" && block.contains("content") &&
                               block["content"].is_array()) {
                        for (const auto& r : block["content"]) {
                            if (r.value("type", "") == "web_search_result") {
                                sources.push_back({r.value("title", ""), r.value("url", "")});
                            }
                        }
                    }
                }
            }
            std::string out = text.empty() ? ("No answer for: " + query) : text;
            if (!sources.empty()) {
                out += "\n\nSources:\n";
                int n = 0;
                for (const auto& s : sources) {
                    if (n++ >= 10) break;
                    out += "- " + s.first + " — " + s.second + "\n";
                }
            }
            return ToolResult::ok(out);
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
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "CloseCrab-Unified/0.2");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        applyProxyToCurl(curl);   // the FIX: honor proxy / CLOSECRAB_NO_PROXY

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return ToolResult::fail(std::string("Fetch failed: ") + curl_easy_strerror(res) +
                                    " (proxy=" + (resolveProxyUrl().empty() ? "none" : resolveProxyUrl()) + ")");
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
