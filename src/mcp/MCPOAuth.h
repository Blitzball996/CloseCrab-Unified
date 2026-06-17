#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <spdlog/spdlog.h>

namespace closecrab {

struct OAuthConfig {
    std::string clientId;
    std::string clientSecret;
    std::string authUrl;        // e.g. https://github.com/login/oauth/authorize
    std::string tokenUrl;       // e.g. https://github.com/login/oauth/access_token
    std::string redirectUri;    // http://localhost:{port}/callback
    std::string scope;          // e.g. "repo,read:org"
};

struct OAuthToken {
    std::string accessToken;
    std::string refreshToken;
    std::string tokenType;
    int expiresIn = 0;
    long long obtainedAt = 0;
};

class MCPOAuth {
public:
    // Start OAuth flow: opens browser, waits for callback, returns token
    static OAuthToken authorize(const OAuthConfig& config, int port = 9876) {
        OAuthToken token;
        std::atomic<bool> received{false};
        std::string authCode;

        // Start local callback server
        httplib::Server server;
        server.Get("/callback", [&](const httplib::Request& req, httplib::Response& res) {
            if (req.has_param("code")) {
                authCode = req.get_param_value("code");
                received = true;
                res.set_content("<html><body><h2>Authorization successful!</h2>"
                    "<p>You can close this window.</p></body></html>", "text/html");
            } else {
                std::string error = req.get_param_value("error");
                res.set_content("<html><body><h2>Error: " + error + "</h2></body></html>", "text/html");
                received = true;
            }
        });

        // Start server in background
        std::thread serverThread([&]() { server.listen("127.0.0.1", port); });

        // Build authorize URL and open browser
        std::string redirectUri = "http://localhost:" + std::to_string(port) + "/callback";
        std::string url = config.authUrl + "?client_id=" + config.clientId
            + "&redirect_uri=" + redirectUri
            + "&scope=" + config.scope
            + "&response_type=code";

        openBrowser(url);

        // Wait for callback (max 120 seconds)
        for (int i = 0; i < 120 && !received; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        server.stop();
        if (serverThread.joinable()) serverThread.join();

        if (authCode.empty()) return token;

        // Exchange the authorization code for an access token at tokenUrl.
        token = exchangeCode(config, authCode, redirectUri);
        return token;
    }

    // True when there is no token or it has (nearly) expired. A 60s skew guard
    // means we refresh slightly early rather than racing expiry.
    static bool isExpired(const OAuthToken& token) {
        if (token.accessToken.empty()) return true;
        if (token.expiresIn <= 0) return false; // no expiry info → assume valid
        long long now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return now >= (token.obtainedAt + token.expiresIn - 60);
    }

    // Exchange an authorization code for tokens (RFC 6749 §4.1.3).
    static OAuthToken exchangeCode(const OAuthConfig& config,
                                   const std::string& code,
                                   const std::string& redirectUri) {
        httplib::Params params;
        params.emplace("grant_type", "authorization_code");
        params.emplace("code", code);
        params.emplace("redirect_uri", redirectUri);
        params.emplace("client_id", config.clientId);
        if (!config.clientSecret.empty()) params.emplace("client_secret", config.clientSecret);
        return postToken(config.tokenUrl, params);
    }

    // Refresh an access token using a refresh token (RFC 6749 §6).
    static OAuthToken refresh(const OAuthConfig& config, const std::string& refreshToken) {
        httplib::Params params;
        params.emplace("grant_type", "refresh_token");
        params.emplace("refresh_token", refreshToken);
        params.emplace("client_id", config.clientId);
        if (!config.clientSecret.empty()) params.emplace("client_secret", config.clientSecret);
        if (!config.scope.empty()) params.emplace("scope", config.scope);
        auto token = postToken(config.tokenUrl, params);
        // Some providers don't echo the refresh token on refresh; keep the old.
        if (token.refreshToken.empty()) token.refreshToken = refreshToken;
        return token;
    }

    // Save token to disk
    static void saveToken(const std::string& serverName, const OAuthToken& token) {
        namespace fs = std::filesystem;
        fs::create_directories("data/mcp-tokens");
        nlohmann::json j;
        j["access_token"] = token.accessToken;
        j["refresh_token"] = token.refreshToken;
        j["token_type"] = token.tokenType;
        j["expires_in"] = token.expiresIn;
        j["obtained_at"] = token.obtainedAt;
        std::ofstream f("data/mcp-tokens/" + serverName + ".json");
        if (f.is_open()) f << j.dump(2);
    }

    // Load token from disk
    static OAuthToken loadToken(const std::string& serverName) {
        OAuthToken token;
        std::string path = "data/mcp-tokens/" + serverName + ".json";
        if (!std::filesystem::exists(path)) return token;
        std::ifstream f(path);
        if (!f.is_open()) return token;
        try {
            nlohmann::json j = nlohmann::json::parse(f);
            token.accessToken = j.value("access_token", "");
            token.refreshToken = j.value("refresh_token", "");
            token.tokenType = j.value("token_type", "bearer");
            token.expiresIn = j.value("expires_in", 0);
            token.obtainedAt = j.value("obtained_at", (long long)0);
        } catch (...) {}
        return token;
    }

private:
    // POST application/x-www-form-urlencoded to a token endpoint and parse the
    // JSON token response. Splits scheme://host[:port]/path for httplib.
    static OAuthToken postToken(const std::string& tokenUrl, const httplib::Params& params) {
        OAuthToken token;
        if (tokenUrl.empty()) return token;

        // Split URL into "scheme://host[:port]" and "/path".
        std::string base = tokenUrl, path = "/";
        auto schemePos = tokenUrl.find("://");
        if (schemePos != std::string::npos) {
            auto pathPos = tokenUrl.find('/', schemePos + 3);
            if (pathPos != std::string::npos) {
                base = tokenUrl.substr(0, pathPos);
                path = tokenUrl.substr(pathPos);
            }
        }

        try {
            httplib::Client cli(base.c_str());
            cli.set_follow_location(true);
            cli.set_connection_timeout(15);
            cli.set_read_timeout(15);
            httplib::Headers headers = {{"Accept", "application/json"}};
            auto res = cli.Post(path.c_str(), headers, params);
            if (!res) {
                spdlog::warn("MCP OAuth: token request to {} failed (no response)", tokenUrl);
                return token;
            }
            if (res->status < 200 || res->status >= 300) {
                spdlog::warn("MCP OAuth: token endpoint returned {} - {}", res->status, res->body);
                return token;
            }
            parseTokenBody(res->body, token);
        } catch (const std::exception& e) {
            spdlog::warn("MCP OAuth: token request error: {}", e.what());
        }
        return token;
    }

    // Parse a token response. Handles JSON; falls back to form-encoded bodies
    // (some providers, e.g. classic GitHub, return access_token=...&...).
    static void parseTokenBody(const std::string& body, OAuthToken& token) {
        bool parsed = false;
        try {
            auto j = nlohmann::json::parse(body);
            token.accessToken  = j.value("access_token", "");
            token.refreshToken = j.value("refresh_token", "");
            token.tokenType    = j.value("token_type", "bearer");
            token.expiresIn    = j.value("expires_in", 0);
            parsed = true;
        } catch (...) { /* not JSON; try form-encoded below */ }

        if (!parsed || token.accessToken.empty()) {
            // form-encoded: a=b&c=d
            auto getField = [&](const std::string& key) -> std::string {
                std::string needle = key + "=";
                auto p = body.find(needle);
                if (p == std::string::npos) return "";
                p += needle.size();
                auto end = body.find('&', p);
                return body.substr(p, end == std::string::npos ? std::string::npos : end - p);
            };
            std::string at = getField("access_token");
            if (!at.empty()) {
                token.accessToken  = at;
                token.refreshToken = getField("refresh_token");
                token.tokenType    = getField("token_type");
                std::string exp = getField("expires_in");
                if (!exp.empty()) { try { token.expiresIn = std::stoi(exp); } catch (...) {} }
            }
        }

        if (!token.accessToken.empty()) {
            token.obtainedAt = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }

    static void openBrowser(const std::string& url) {
#ifdef _WIN32
        std::string cmd = "start \"\" \"" + url + "\"";
        system(cmd.c_str());
#elif __APPLE__
        system(("open \"" + url + "\"").c_str());
#else
        system(("xdg-open \"" + url + "\"").c_str());
#endif
    }
};

} // namespace closecrab
