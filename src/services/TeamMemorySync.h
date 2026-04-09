#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <regex>
#include <fstream>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {
namespace fs = std::filesystem;

class TeamMemorySync {
public:
    static TeamMemorySync& getInstance() {
        static TeamMemorySync instance;
        return instance;
    }

    struct SyncResult {
        int uploaded = 0;
        int downloaded = 0;
        int conflicts = 0;
        int secretsBlocked = 0;
    };

    SyncResult sync(const std::string& projectRoot, const std::string& teamEndpoint) {
        std::lock_guard<std::mutex> lock(mutex_);
        SyncResult result;
        fs::path memDir = fs::path(projectRoot) / ".claude" / "memory";
        fs::path teamDir = memDir / "team";

        if (!fs::exists(memDir)) { spdlog::warn("No memory directory"); return result; }
        if (!fs::exists(teamDir)) fs::create_directories(teamDir);

        // Upload local memories (scan for secrets first)
        for (const auto& entry : fs::directory_iterator(memDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
            if (entry.path().parent_path().filename() == "team") continue;

            std::ifstream f(entry.path());
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

            auto secrets = scanForSecrets(content);
            if (!secrets.empty()) {
                spdlog::warn("Blocked upload of {} — contains {} potential secrets",
                             entry.path().filename().string(), secrets.size());
                result.secretsBlocked++;
                continue;
            }
            // Would POST to teamEndpoint/upload here
            result.uploaded++;
        }

        // Download team memories (would GET from teamEndpoint/download)
        // For now, just report what we'd sync
        spdlog::info("Team sync: uploaded={}, downloaded={}, conflicts={}, secretsBlocked={}",
                     result.uploaded, result.downloaded, result.conflicts, result.secretsBlocked);
        return result;
    }

    SyncResult deltaSync(const std::string& projectRoot, const std::string& teamEndpoint) {
        // Delta sync uses ETag for incremental updates
        // Would send If-None-Match: lastETag_ header
        return sync(projectRoot, teamEndpoint);
    }

    std::vector<std::string> scanForSecrets(const std::string& content) {
        std::vector<std::string> found;
        static const std::vector<std::pair<std::string, std::regex>> patterns = {
            {"AWS Access Key", std::regex(R"(AKIA[0-9A-Z]{16})")},
            {"GitHub Token", std::regex(R"(gh[ps]_[A-Za-z0-9_]{36,})")},
            {"Generic API Key", std::regex(R"((?:api[_-]?key|apikey)\s*[:=]\s*['\"]?[A-Za-z0-9_\-]{20,})", std::regex::icase)},
            {"Password in Config", std::regex(R"((?:password|passwd|pwd)\s*[:=]\s*['\"]?[^\s'\"]{8,})", std::regex::icase)},
            {"Private Key", std::regex(R"(-----BEGIN (?:RSA |EC |DSA )?PRIVATE KEY-----)")},
            {"Slack Token", std::regex(R"(xox[bpors]-[0-9A-Za-z\-]{10,})")},
            {"Generic Secret", std::regex(R"((?:secret|token)\s*[:=]\s*['\"]?[A-Za-z0-9_\-]{20,})", std::regex::icase)},
        };

        for (const auto& [name, pattern] : patterns) {
            std::smatch match;
            if (std::regex_search(content, match, pattern)) {
                found.push_back(name + ": " + match[0].str().substr(0, 20) + "...");
            }
        }
        return found;
    }

private:
    TeamMemorySync() = default;
    std::string lastETag_;
    std::mutex mutex_;
};

} // namespace closecrab
