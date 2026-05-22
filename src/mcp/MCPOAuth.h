#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <httplib.h>

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

        // Exchange code for token (would need CURL in real impl)
        token.accessToken = authCode; // Placeholder — real impl exchanges via tokenUrl
        token.obtainedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
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
