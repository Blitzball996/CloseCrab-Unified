#pragma once

#include <string>
#include <map>
#include <mutex>
#include <filesystem>

namespace closecrab {

// File content cache with mtime-based invalidation
// Avoids re-reading files that haven't changed between tool calls
class FileStateCache {
public:
    static FileStateCache& getInstance() {
        static FileStateCache instance;
        return instance;
    }

    struct CachedFile {
        std::string content;
        std::filesystem::file_time_type mtime;
        size_t size = 0;
    };

    // Get cached content if file hasn't changed, returns true if cache hit
    bool get(const std::string& path, std::string& content) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(path);
        if (it == cache_.end()) return false;

        try {
            auto currentMtime = std::filesystem::last_write_time(path);
            if (currentMtime == it->second.mtime) {
                content = it->second.content;
                return true;
            }
            // File changed, invalidate
            cache_.erase(it);
        } catch (...) {
            cache_.erase(it);
        }
        return false;
    }

    void put(const std::string& path, const std::string& content) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Evict if cache too large (>50MB total or >200 files)
        if (cache_.size() > 200 || totalSize_ > 50 * 1024 * 1024) {
            evictOldest();
        }

        try {
            CachedFile cf;
            cf.content = content;
            cf.mtime = std::filesystem::last_write_time(path);
            cf.size = content.size();
            totalSize_ += cf.size;
            cache_[path] = std::move(cf);
        } catch (...) {}
    }

    void invalidate(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(path);
        if (it != cache_.end()) {
            totalSize_ -= it->second.size;
            cache_.erase(it);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        totalSize_ = 0;
    }

    size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return cache_.size(); }
    size_t totalBytes() const { return totalSize_; }

private:
    FileStateCache() = default;

    void evictOldest() {
        // Remove ~25% of entries (oldest by mtime)
        size_t toRemove = cache_.size() / 4;
        if (toRemove == 0) toRemove = 1;

        std::vector<std::pair<std::filesystem::file_time_type, std::string>> entries;
        for (const auto& [path, cf] : cache_) {
            entries.push_back({cf.mtime, path});
        }
        std::sort(entries.begin(), entries.end());

        for (size_t i = 0; i < toRemove && i < entries.size(); i++) {
            auto it = cache_.find(entries[i].second);
            if (it != cache_.end()) {
                totalSize_ -= it->second.size;
                cache_.erase(it);
            }
        }
    }

    mutable std::mutex mutex_;
    std::map<std::string, CachedFile> cache_;
    size_t totalSize_ = 0;
};

} // namespace closecrab
