#pragma once

#include "../Tool.h"
#include "../../api/APIClient.h"
#include "../../core/Message.h"
#include "../../config/Config.h"
#include "../../utils/ProxyConfig.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <map>
#include <mutex>
#include <chrono>
#include <string>
#include <vector>
#include <utility>

namespace closecrab {

// Simple URL cache with 15-minute TTL (used by WebFetch)
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
// PLACEHOLDER_WEBSEARCH
// ---------------------------------------------------------------------------
// WebSearchTool — server-side web search, FULLY aligned with JackProAi.
//
// Like JackProAi (which calls queryModelWithStreaming for its web_search
// sub-query), we run the search through the SAME API path as the main loop —
// ctx.apiClient->streamChat — instead of a hand-rolled curl. This inherits
// EVERYTHING that makes the main loop work on macOS: forced HTTP/1.1 (system
// libcurl negotiates HTTP/2 otherwise → the mac-only 503), the full Claude Code
// User-Agent / beta headers, SSL verify, 10x retry + opus->fallback model
// switch, and proxy handling. The server runs web_search_20250305 and streams
// back text + web_search_tool_result blocks; we collect them via the callback.
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

        // Server-side web search needs the hosted Anthropic API path. In local-LLM
        // mode there is no endpoint to run it.
        if (!ctx.apiClient || ctx.apiClient->isLocal()) {
            return ToolResult::fail(
                "WebSearch needs the hosted API (provider: anthropic). "
                "Set it with /api provider=anthropic and /api key=sk-... , then restart.");
        }

        // Single-message sub-query (JackProAi: messages:[userMessage]). Tiny
        // context — does NOT carry the main conversation, so it can't bloat.
        std::vector<Message> messages;
        messages.push_back(Message::makeUser(
            "Perform a web search and answer concisely, citing sources: " + query));

        ModelConfig cfg;
        cfg.maxTokens = 2048;
        cfg.stream = true;                 // streamChat path
        cfg.thinkingEnabled = false;
        cfg.abortFlag = ctx.abortFlag;
        // The server-side web_search tool (JackProAi makeToolSchema, max_uses=8).
        cfg.extraServerTools = nlohmann::json::array({
            {{"type", "web_search_20250305"}, {"name", "web_search"}, {"max_uses", 8}}
        });

        // Collect the streamed answer text + (title,url) hits.
        std::string answer;
        std::vector<std::pair<std::string,std::string>> sources;
        std::string apiError;
        try {
            ctx.apiClient->streamChat(messages, /*systemPrompt*/
                "You are an assistant performing a web search. Be concise.",
                cfg, [&](const StreamEvent& ev) {
                    if (ev.type == StreamEvent::EVT_TEXT) {
                        answer += ev.content;
                    } else if (ev.type == StreamEvent::EVT_WEB_SEARCH_RESULT &&
                               ev.toolInput.is_array()) {
                        for (const auto& h : ev.toolInput) {
                            sources.push_back({h.value("title", ""), h.value("url", "")});
                        }
                    } else if (ev.type == StreamEvent::EVT_ERROR) {
                        apiError = ev.content;
                    }
                });
        } catch (const std::exception& e) {
            return ToolResult::fail(std::string("Search failed: ") + e.what());
        }

        if (answer.empty() && sources.empty()) {
            if (!apiError.empty()) return ToolResult::fail("Search failed: " + apiError);
            return ToolResult::ok("No results found for: " + query);
        }

        // Format like JackProAi mapToolResultToToolResultBlockParam: the answer,
        // then the sources, then a REMINDER to cite them. encrypted_content was
        // already stripped (handleSSEEvent keeps only title/url).
        std::string out = "Web search results for query: \"" + query + "\"\n\n";
        if (!answer.empty()) out += answer + "\n\n";
        if (!sources.empty()) {
            out += "Sources:\n";
            int n = 0;
            for (const auto& s : sources) {
                if (n++ >= 10) break;
                out += "- " + s.first + " — " + s.second + "\n";
            }
            out += "\nREMINDER: cite the sources above using markdown hyperlinks.";
        }
        return ToolResult::ok(out);
    }
};
// PLACEHOLDER_WEBFETCH
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
        // Force HTTP/1.1: macOS system libcurl negotiates HTTP/2 by default, which
        // is flaky with some proxies/relays (the mac-only failure class). Match
        // RemoteAPIClient, which forces 1.1 for exactly this reason.
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        applyProxyToCurl(curl);   // honor proxy / CLOSECRAB_NO_PROXY

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return ToolResult::fail(std::string("Fetch failed: ") + curl_easy_strerror(res) +
                                    " (proxy=" + (resolveProxyUrl().empty() ? "none" : resolveProxyUrl()) + ")");
        }

        if (response.size() > 50000) {
            response = response.substr(0, 50000) + "\n... (truncated)";
        }

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
