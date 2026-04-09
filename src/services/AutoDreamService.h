#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <functional>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {
namespace fs = std::filesystem;

class AutoDreamService {
public:
    static AutoDreamService& getInstance() {
        static AutoDreamService instance;
        return instance;
    }

    bool shouldFire(int sessionCount, std::chrono::steady_clock::time_point lastFireTime) const {
        if (sessionCount < 5) return false;
        auto elapsed = std::chrono::steady_clock::now() - lastFireTime;
        return elapsed >= std::chrono::hours(1);
    }

    // ApiClient is a template param so this stays header-only and decoupled.
    template <typename ApiClient>
    void fire(const std::string& projectRoot, ApiClient* apiClient) {
        std::lock_guard<std::mutex> lock(mutex_);

        fs::path memDir = fs::path(projectRoot) / ".claude" / "memory";
        if (!fs::exists(memDir) || !fs::is_directory(memDir)) {
            spdlog::info("AutoDream: no memory directory at {}", memDir.string());
            return;
        }

        // Step 1: Load all memory files
        std::vector<MemoryEntry> entries;
        for (const auto& entry : fs::directory_iterator(memDir)) {
            if (!entry.is_regular_file()) continue;
            try {
                std::ifstream f(entry.path());
                std::string content((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
                entries.push_back({entry.path(), content});
            } catch (const std::exception& e) {
                spdlog::warn("AutoDream: failed to read {}: {}", entry.path().string(), e.what());
            }
        }

        if (entries.empty()) {
            spdlog::info("AutoDream: no memory files found");
            return;
        }

        spdlog::info("AutoDream: loaded {} memory files", entries.size());

        // Step 2: Consolidate
        if (apiClient) {
            consolidateWithLLM(entries, memDir, apiClient);
        } else {
            removeDuplicates(entries, memDir);
        }
    }

private:
    AutoDreamService() = default;

    struct MemoryEntry {
        fs::path path;
        std::string content;
    };

    mutable std::mutex mutex_;

    template <typename ApiClient>
    void consolidateWithLLM(const std::vector<MemoryEntry>& entries,
                            const fs::path& memDir,
                            ApiClient* apiClient) {
        // Build JSON array of all memories
        nlohmann::json memoriesJson = nlohmann::json::array();
        for (const auto& e : entries) {
            memoriesJson.push_back({
                {"file", e.path.filename().string()},
                {"content", e.content}
            });
        }

        std::string prompt =
            "Consolidate these memories. Remove duplicates, merge related items, "
            "update outdated info. Return consolidated memories as JSON array.\n\n"
            + memoriesJson.dump(2);

        try {
            std::string response = apiClient->complete(prompt);

            // Parse the JSON array from the response
            // Try to find JSON array in the response
            auto startPos = response.find('[');
            auto endPos = response.rfind(']');
            if (startPos == std::string::npos || endPos == std::string::npos || endPos <= startPos) {
                spdlog::warn("AutoDream: LLM response did not contain a JSON array");
                return;
            }

            auto consolidated = nlohmann::json::parse(
                response.substr(startPos, endPos - startPos + 1));

            if (!consolidated.is_array()) {
                spdlog::warn("AutoDream: parsed response is not an array");
                return;
            }

            // Remove old files
            for (const auto& e : entries) {
                std::error_code ec;
                fs::remove(e.path, ec);
            }

            // Write consolidated files
            int idx = 0;
            for (const auto& item : consolidated) {
                std::string filename = "memory_" + std::to_string(idx++) + ".md";
                if (item.contains("file") && item["file"].is_string()) {
                    filename = item["file"].get<std::string>();
                }
                std::string content = item.value("content", item.dump(2));

                std::ofstream out(memDir / filename);
                out << content;
            }

            spdlog::info("AutoDream: consolidated {} memories into {} entries",
                         entries.size(), consolidated.size());

        } catch (const std::exception& e) {
            spdlog::error("AutoDream: LLM consolidation failed: {}", e.what());
            // Fallback to local dedup
            removeDuplicates(entries, memDir);
        }
    }

    void removeDuplicates(const std::vector<MemoryEntry>& entries, const fs::path& /*memDir*/) {
        // Remove files with exact duplicate content, keeping the first occurrence
        std::vector<std::string> seen;
        int removed = 0;

        for (const auto& e : entries) {
            bool isDuplicate = false;
            for (const auto& s : seen) {
                if (s == e.content) {
                    isDuplicate = true;
                    break;
                }
            }

            if (isDuplicate) {
                std::error_code ec;
                fs::remove(e.path, ec);
                if (!ec) {
                    ++removed;
                    spdlog::debug("AutoDream: removed duplicate {}", e.path.string());
                }
            } else {
                seen.push_back(e.content);
            }
        }

        spdlog::info("AutoDream: removed {} exact duplicate memory files", removed);
    }
};

} // namespace closecrab
