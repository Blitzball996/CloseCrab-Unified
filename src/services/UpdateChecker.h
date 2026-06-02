#pragma once
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "../utils/ProxyConfig.h"

namespace closecrab {

struct VersionInfo {
    std::string currentVersion = "0.1.0";
    std::string latestVersion;
    std::string downloadUrl;
    std::string releaseNotes;
    bool updateAvailable = false;
};

class UpdateChecker {
public:
    static VersionInfo check(const std::string& repo = "Blitzball996/CloseCrab-Unified") {
        VersionInfo info;
        std::string url = "https://api.github.com/repos/" + repo + "/releases/latest";
        std::string response = httpGet(url);
        if (response.empty()) return info;

        try {
            auto j = nlohmann::json::parse(response);
            info.latestVersion = j.value("tag_name", "");
            info.releaseNotes = j.value("body", "");

            // Check assets for download URL
            if (j.contains("assets") && j["assets"].is_array()) {
                for (const auto& asset : j["assets"]) {
                    std::string name = asset.value("name", "");
                    if (name.find(".exe") != std::string::npos || name.find("Setup") != std::string::npos) {
                        info.downloadUrl = asset.value("browser_download_url", "");
                        break;
                    }
                }
            }

            // Compare versions (simple string compare, works for semver)
            if (!info.latestVersion.empty() && info.latestVersion != info.currentVersion) {
                // Strip 'v' prefix if present
                std::string latest = info.latestVersion;
                if (!latest.empty() && latest[0] == 'v') latest = latest.substr(1);
                if (latest > info.currentVersion) {
                    info.updateAvailable = true;
                }
            }
        } catch (const std::exception& e) {
            spdlog::debug("Update check failed: {}", e.what());
        }
        return info;
    }

    static std::string getUpdateMessage(const VersionInfo& info) {
        if (!info.updateAvailable) return "";
        std::string msg = "\033[33mUpdate available: " + info.currentVersion
            + " -> " + info.latestVersion + "\033[0m\n";
        if (!info.downloadUrl.empty()) {
            msg += "  Download: " + info.downloadUrl + "\n";
        }
        return msg;
    }

private:
    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, std::string* data) {
        data->append(ptr, size * nmemb);
        return size * nmemb;
    }

    static std::string httpGet(const std::string& url) {
        std::string response;
        CURL* curl = curl_easy_init();
        if (!curl) return response;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "CloseCrab-Unified/0.1.0");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        applyProxyToCurl(curl);   // GitHub API is unreliable behind the GFW without a proxy

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            spdlog::debug("HTTP GET failed: {}", curl_easy_strerror(res));
            return "";
        }
        return response;
    }
};

} // namespace closecrab
