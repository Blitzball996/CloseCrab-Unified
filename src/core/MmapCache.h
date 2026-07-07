#pragma once

#include <string>
#include <unordered_map>
#include <list>
#include <memory>
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
///
/// SAFETY: acquire() returns a shared_ptr "pin". The underlying view is
/// guaranteed valid for as long as the caller holds the pin — invalidate(),
/// LRU eviction and mtime-change replacement only drop the CACHE's reference;
/// the actual UnmapViewOfFile/munmap runs when the last pin is released.
/// (Previously get()/map() handed out raw pointers that another thread's
/// invalidate()/evict could unmap mid-scan → ACCESS_VIOLATION in memchr/
/// memmove on reader threads.)
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

    /// A pin keeps the mapping alive while the caller reads from it.
    using Pin = std::shared_ptr<const MappedFile>;

    /// Get a pinned mapping for `path` — cache hit (mtime unchanged) or fresh map.
    /// Returns nullptr if the file can't be opened/mapped (caller should fall
    /// back to ifstream). For an empty file, returns a pin with data==nullptr
    /// and size==0.
    Pin acquire(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = lookup_.find(path);
        if (it != lookup_.end()) {
            std::error_code ec;
            auto currentMtime = std::filesystem::last_write_time(path, ec);
            if (!ec && currentMtime == it->second->entry->mtime) {
                lru_.splice(lru_.begin(), lru_, it->second); // promote
                return it->second->entry;
            }
            removeNode(it->second); // stale — drop cache ref (unmap deferred to pins)
        }

        // Fresh map. Deleter unmaps when the LAST reference (cache or pin) dies.
        std::shared_ptr<MappedFile> mf(new MappedFile{},
                                       [](MappedFile* p) { doUnmap(*p); delete p; });
        if (!doMap(path, *mf)) return nullptr;

        // Evict until we're within limits (unmap of evicted entries is deferred
        // if some reader still pins them).
        while ((lru_.size() >= MAX_FILES || totalBytes_ + mf->size > MAX_BYTES) && !lru_.empty()) {
            evictLRU();
        }

        // Single file exceeding the cap: hand it out uncached; it unmaps as
        // soon as the caller drops the pin.
        if (mf->size > MAX_BYTES) return mf;

        lru_.emplace_front(LRUNode{path, mf});
        lookup_[path] = lru_.begin();
        totalBytes_ += mf->size;
        return mf;
    }

    /// Invalidate a specific path (e.g. after a write tool modifies it, or to
    /// release the Windows file lock a mapping holds). Only drops the cache's
    /// reference — outstanding pins stay valid until released.
    void invalidate(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lookup_.find(path);
        if (it != lookup_.end()) {
            removeNode(it->second);
        }
    }

    /// Clear all cached mappings (pins held by readers stay valid).
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
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
        std::shared_ptr<MappedFile> entry;
    };

    using LRUList = std::list<LRUNode>;
    using LRUIter = LRUList::iterator;

    mutable std::mutex mutex_;
    LRUList lru_;                                    // front = most recent
    std::unordered_map<std::string, LRUIter> lookup_;
    size_t totalBytes_ = 0;

    // --- Platform mmap/unmap ---

    static bool doMap(const std::string& path, MappedFile& mf) {
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

    static void doUnmap(MappedFile& mf) {
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
        totalBytes_ -= back.entry->size;
        lookup_.erase(back.path);
        lru_.pop_back();   // unmap deferred: shared_ptr deleter fires on last ref
    }

    void removeNode(LRUIter it) {
        totalBytes_ -= it->entry->size;
        lookup_.erase(it->path);
        lru_.erase(it);    // unmap deferred: shared_ptr deleter fires on last ref
    }
};

} // namespace closecrab
