#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <queue>
#include <condition_variable>

namespace closecrab {

// Predictive Engine: two C++-exclusive optimizations that TS projects can't do.
//
// 1. Idle Precomputation: while waiting for API response, preload commonly
//    needed data (git status, directory listings, recent files) so tools
//    can return instantly when called.
//
// 2. Speculative Tool Execution: when streaming reveals a tool_use with a
//    known tool name (e.g., "Read"), start executing as soon as enough of
//    the input JSON is available (don't wait for content_block_stop).

struct PreloadedData {
    std::string content;
    int64_t mtime = 0;
    std::chrono::steady_clock::time_point loadedAt;
};

class PredictiveEngine {
public:
    static PredictiveEngine& getInstance() {
        static PredictiveEngine instance;
        return instance;
    }

    // === Idle Precomputation ===

    void startPreloading(const std::string& cwd) {
        if (preloading_.load()) return;
        preloading_ = true;
        cwd_ = cwd;
        preloadThread_ = std::thread([this]() { runPreload(); });
    }

    void stopPreloading() {
        preloading_ = false;
        if (preloadThread_.joinable()) preloadThread_.join();
    }

    // Check if a file is already preloaded (tools call this before reading)
    bool getPreloaded(const std::string& path, std::string& out) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = fileCache_.find(path);
        if (it == fileCache_.end()) return false;
        auto age = std::chrono::steady_clock::now() - it->second.loadedAt;
        if (age > std::chrono::seconds(30)) {
            fileCache_.erase(it);
            return false;
        }
        out = it->second.content;
        return true;
    }

    // === Speculative Tool Execution ===

    // Called when streaming reveals tool name (content_block_start)
    void onToolHint(const std::string& toolName, const std::string& partialJson) {
        if (toolName == "Read" || toolName == "Glob") {
            std::string path = extractPath(partialJson);
            if (!path.empty()) {
                speculativeRead(path);
            }
        }
    }

    // Called as input_json_delta arrives — try to extract path early
    void onToolInputDelta(const std::string& toolName, const std::string& accumulatedJson) {
        if (toolName != "Read" && toolName != "Glob") return;
        std::string path = extractPath(accumulatedJson);
        if (!path.empty()) {
            speculativeRead(path);
        }
    }

    int getCacheHits() const { return cacheHits_.load(); }
    int getCacheMisses() const { return cacheMisses_.load(); }

private:
    PredictiveEngine() = default;

    // Join the preload thread before destruction. Without this, a turn that
    // started preloading leaves preloadThread_ joinable at program exit; the
    // singleton's destructor then runs std::thread::~thread() on a joinable
    // thread → std::terminate ("bad exception"). This was the shutdown crash
    // after any turn that triggered preloading (seen on API failure, but the
    // root cause is the missing join, not the API error). stopPreloading()
    // clears the flag (runPreload checks it) and joins, so this returns promptly.
    ~PredictiveEngine() { stopPreloading(); }

    void runPreload() {
        namespace fs = std::filesystem;
        try {
            // Preload: list files in CWD (for Glob)
            std::vector<std::string> recentFiles;
            for (auto& entry : fs::directory_iterator(cwd_)) {
                if (!preloading_.load()) return;
                if (entry.is_regular_file()) {
                    recentFiles.push_back(entry.path().string());
                }
            }

            // Preload: read small files that are likely to be needed
            for (auto& path : recentFiles) {
                if (!preloading_.load()) return;
                try {
                    auto size = fs::file_size(path);
                    if (size > 100 * 1024) continue; // Skip files > 100KB
                    std::ifstream ifs(path, std::ios::binary);
                    if (!ifs) continue;
                    std::string content((std::istreambuf_iterator<char>(ifs)),
                                        std::istreambuf_iterator<char>());
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    if (fileCache_.size() >= 50) return; // Cache full
                    fileCache_[path] = {std::move(content), 0,
                                        std::chrono::steady_clock::now()};
                } catch (...) {}
            }
        } catch (...) {}
    }

    void speculativeRead(const std::string& path) {
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            if (fileCache_.count(path)) return; // Already cached
        }
        // Read in background
        std::thread([this, path]() {
            try {
                namespace fs = std::filesystem;
                if (!fs::exists(path)) return;
                auto size = fs::file_size(path);
                if (size > 200 * 1024) return; // Don't cache huge files
                std::ifstream ifs(path, std::ios::binary);
                if (!ifs) return;
                std::string content((std::istreambuf_iterator<char>(ifs)),
                                    std::istreambuf_iterator<char>());
                std::lock_guard<std::mutex> lock(cacheMutex_);
                fileCache_[path] = {std::move(content), 0,
                                    std::chrono::steady_clock::now()};
            } catch (...) {}
        }).detach();
    }

    static std::string extractPath(const std::string& json) {
        // Fast extraction of file_path from partial JSON
        // Look for "file_path": "..." or "path": "..."
        auto extract = [&](const std::string& key) -> std::string {
            size_t pos = json.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            pos = json.find('"', pos + key.size() + 2);
            if (pos == std::string::npos) return "";
            pos++; // skip opening quote
            size_t end = json.find('"', pos);
            if (end == std::string::npos) return ""; // Path not complete yet
            return json.substr(pos, end - pos);
        };
        std::string path = extract("file_path");
        if (path.empty()) path = extract("path");
        if (path.empty()) path = extract("pattern"); // For Glob
        return path;
    }

    std::atomic<bool> preloading_{false};
    std::string cwd_;
    std::thread preloadThread_;
    std::mutex cacheMutex_;
    std::unordered_map<std::string, PreloadedData> fileCache_;
    std::atomic<int> cacheHits_{0};
    std::atomic<int> cacheMisses_{0};
};

} // namespace closecrab
