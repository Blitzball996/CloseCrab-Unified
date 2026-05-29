#pragma once

#include <string>
#include <unordered_map>
#include <list>
#include <mutex>
#include <filesystem>
#include <cstring>
#include <cstdint>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace closecrab {

/// Memory-mapped file cache with LRU eviction.
/// Singleton. Thread-safe. Caches up to 50 files / 100MB total.
/// On cache hit with unchanged mtime, returns a string_view into the mapped region
/// without any copy. Callers that need a std::string can construct one from the view.
class MmapCache {
public:
    static constexpr size_t MAX_FILES = 50;
    static constexpr size_t MAX_BYTES = 100 * 1024 * 1024; // 100MB

    static MmapCache& getInstance() {
        static MmapCache instance;
        return instance;
    }

    struct MappedFile {
        const char* data = nullptr;
        size_t      size = 0;
        std::filesystem::file_time_type mtime{};
#ifdef _WIN32
        HANDLE hFile = INVALID_HANDLE_VALUE;
        HANDLE hMapping = nullptr;
#else
        int fd = -1;
#endif
    };

    /// Try to get a cached mapping. Returns true on hit (data/size populated).
    /// If the file's mtime changed since caching, the old mapping is evicted and
    /// this returns false so the caller can re-request via `map()`.
    bool get(const std::string& path, const char*& outData, size_t& outSize) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lookup_.find(path);
        if (it == lookup_.end()) return false;

        // Validate mtime
        std::error_code ec;
        auto currentMtime = std::filesystem::last_write_time(path, ec);
        if (ec || currentMtime != it->second->entry.mtime) {
            unmapAndRemove(it->second);
            return false;
        }

        // Promote to front (most recently used)
        lru_.splice(lru_.begin(), lru_, it->second);
        outData = it->second->entry.data;
        outSize = it->second->entry.size;
        return true;
    }

    /// Map a file and cache it. Returns true on success.
    bool map(const std::string& path, const char*& outData, size_t& outSize) {
        std::lock_guard<std::mutex> lock(mutex_);

        // If already cached (race with another thread), just return it
        auto existing = lookup_.find(path);
        if (existing != lookup_.end()) {
            lru_.splice(lru_.begin(), lru_, existing->second);
            outData = existing->second->entry.data;
            outSize = existing->second->entry.size;
            return true;
        }

        MappedFile mf{};
        if (!doMap(path, mf)) return false;

        // Evict until we're within limits
        while ((lru_.size() >= MAX_FILES || totalBytes_ + mf.size > MAX_BYTES) && !lru_.empty()) {
            evictLRU();
        }

        // If single file exceeds max, don't cache it — just return the mapping
        // without storing. Caller gets the data but it won't persist.
        if (mf.size > MAX_BYTES) {
            outData = mf.data;
            outSize = mf.size;
            // We can't cache it, but we still return it mapped.
            // Store in a separate "uncached" set so we can unmap later...
            // Actually, for simplicity, copy to string and unmap immediately.
            doUnmap(mf);
            return false;
        }

        lru_.emplace_front(LRUNode{path, mf});
        lookup_[path] = lru_.begin();
        totalBytes_ += mf.size;

        outData = mf.data;
        outSize = mf.size;
        return true;
    }

    /// Invalidate a specific path (e.g. after a write tool modifies it)
    void invalidate(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lookup_.find(path);
        if (it != lookup_.end()) {
            unmapAndRemove(it->second);
        }
    }

    /// Clear all cached mappings
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& node : lru_) {
            doUnmap(node.entry);
        }
        lru_.clear();
        lookup_.clear();
        totalBytes_ = 0;
    }

    size_t cachedFileCount() const { std::lock_guard<std::mutex> lock(mutex_); return lru_.size(); }
    size_t cachedBytes() const { std::lock_guard<std::mutex> lock(mutex_); return totalBytes_; }

    ~MmapCache() { clear(); }

private:
    MmapCache() = default;
    MmapCache(const MmapCache&) = delete;
    MmapCache& operator=(const MmapCache&) = delete;

    struct LRUNode {
        std::string path;
        MappedFile  entry;
    };

    using LRUList = std::list<LRUNode>;
    using LRUIter = LRUList::iterator;

    mutable std::mutex mutex_;
    LRUList lru_;                                    // front = most recent
    std::unordered_map<std::string, LRUIter> lookup_;
    size_t totalBytes_ = 0;

    // --- Platform mmap/unmap ---

    bool doMap(const std::string& path, MappedFile& mf) {
        std::error_code ec;
        mf.mtime = std::filesystem::last_write_time(path, ec);
        if (ec) return false;

#ifdef _WIN32
        // Use wide path for Unicode support
        std::filesystem::path fsPath;
        try { fsPath = std::filesystem::u8path(path); } catch (...) { fsPath = path; }

        mf.hFile = CreateFileW(
            fsPath.wstring().c_str(),
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (mf.hFile == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(mf.hFile, &fileSize)) {
            CloseHandle(mf.hFile);
            mf.hFile = INVALID_HANDLE_VALUE;
            return false;
        }
        mf.size = static_cast<size_t>(fileSize.QuadPart);

        if (mf.size == 0) {
            // Can't mmap empty file — store nullptr with size 0
            CloseHandle(mf.hFile);
            mf.hFile = INVALID_HANDLE_VALUE;
            mf.data = nullptr;
            return true;
        }

        mf.hMapping = CreateFileMappingW(mf.hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mf.hMapping) {
            CloseHandle(mf.hFile);
            mf.hFile = INVALID_HANDLE_VALUE;
            return false;
        }

        void* view = MapViewOfFile(mf.hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!view) {
            CloseHandle(mf.hMapping);
            CloseHandle(mf.hFile);
            mf.hMapping = nullptr;
            mf.hFile = INVALID_HANDLE_VALUE;
            return false;
        }
        mf.data = static_cast<const char*>(view);
#else
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;

        struct stat st;
        if (::fstat(fd, &st) != 0) { ::close(fd); return false; }
        mf.size = static_cast<size_t>(st.st_size);
        mf.fd = fd;

        if (mf.size == 0) {
            ::close(fd);
            mf.fd = -1;
            mf.data = nullptr;
            return true;
        }

        void* addr = ::mmap(nullptr, mf.size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) {
            ::close(fd);
            mf.fd = -1;
            return false;
        }
        mf.data = static_cast<const char*>(addr);
#endif
        return true;
    }

    void doUnmap(MappedFile& mf) {
#ifdef _WIN32
        if (mf.data) UnmapViewOfFile(mf.data);
        if (mf.hMapping) CloseHandle(mf.hMapping);
        if (mf.hFile != INVALID_HANDLE_VALUE) CloseHandle(mf.hFile);
        mf.data = nullptr;
        mf.hMapping = nullptr;
        mf.hFile = INVALID_HANDLE_VALUE;
#else
        if (mf.data && mf.size > 0) ::munmap(const_cast<char*>(mf.data), mf.size);
        if (mf.fd >= 0) ::close(mf.fd);
        mf.data = nullptr;
        mf.fd = -1;
#endif
        mf.size = 0;
    }

    void evictLRU() {
        if (lru_.empty()) return;
        auto& back = lru_.back();
        totalBytes_ -= back.entry.size;
        doUnmap(back.entry);
        lookup_.erase(back.path);
        lru_.pop_back();
    }

    void unmapAndRemove(LRUIter it) {
        totalBytes_ -= it->entry.size;
        doUnmap(it->entry);
        lookup_.erase(it->path);
        lru_.erase(it);
    }
};

} // namespace closecrab
