#pragma once
#include "MCPClient.h"
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {
namespace fs = std::filesystem;

struct MCPRegistryEntry {
    std::string name;
    std::string description;
    std::string command;
    std::vector<std::string> args;
    std::string transport; // "stdio" or "sse"
};

class MCPRegistry {
public:
    static MCPRegistry& getInstance() {
        static MCPRegistry instance;
        return instance;
    }

    std::vector<MCPRegistryEntry> discoverServers(const std::string& query = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cachedEntries_.empty()) refreshCacheInternal();

        if (query.empty()) return cachedEntries_;

        std::vector<MCPRegistryEntry> results;
        std::string lq = toLower(query);
        for (const auto& entry : cachedEntries_) {
            if (toLower(entry.name).find(lq) != std::string::npos ||
                toLower(entry.description).find(lq) != std::string::npos) {
                results.push_back(entry);
            }
        }
        return results;
    }

    void refreshCache() {
        std::lock_guard<std::mutex> lock(mutex_);
        refreshCacheInternal();
    }

    void setRegistryPath(const std::string& path) { registryPath_ = path; }

private:
    MCPRegistry() = default;

    void refreshCacheInternal() {
        cachedEntries_.clear();

        // Load from local registry file
        std::vector<std::string> paths = {
            registryPath_,
            ".claude/mcp-registry.json",
        };
        // Also check home directory
        const char* home = std::getenv("HOME");
        if (!home) home = std::getenv("USERPROFILE");
        if (home) paths.push_back(std::string(home) + "/.claude/mcp-registry.json");

        for (const auto& path : paths) {
            if (path.empty() || !fs::exists(path)) continue;
            try {
                std::ifstream f(path);
                auto j = nlohmann::json::parse(f);
                if (j.is_array()) {
                    for (const auto& entry : j) {
                        MCPRegistryEntry e;
                        e.name = entry.value("name", "");
                        e.description = entry.value("description", "");
                        e.command = entry.value("command", "");
                        e.transport = entry.value("transport", "stdio");
                        if (entry.contains("args") && entry["args"].is_array()) {
                            for (const auto& a : entry["args"]) e.args.push_back(a.get<std::string>());
                        }
                        if (!e.name.empty()) cachedEntries_.push_back(std::move(e));
                    }
                }
                spdlog::info("Loaded {} MCP registry entries from {}", cachedEntries_.size(), path);
                break; // Use first found
            } catch (const std::exception& ex) {
                spdlog::warn("Failed to load MCP registry from {}: {}", path, ex.what());
            }
        }
        lastRefresh_ = std::chrono::steady_clock::now();
    }

    static std::string toLower(const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(), ::tolower);
        return r;
    }

    std::mutex mutex_;
    std::vector<MCPRegistryEntry> cachedEntries_;
    std::chrono::steady_clock::time_point lastRefresh_;
    std::string registryPath_;
};

} // namespace closecrab
