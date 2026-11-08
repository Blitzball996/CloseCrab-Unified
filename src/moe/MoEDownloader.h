#pragma once
// MoEDownloader.h — multi-shard model downloader with resume + SHA-256.
// Downloads all shards of a ModelEntry into models/moe/<id>/ and verifies.

#include "MoEHardwareProbe.h"
#include <atomic>
#include <functional>
#include <string>

namespace closecrab::moe {

struct DownloadProgress {
    int    shardIndex;    // 0-based
    int    shardCount;
    double downloadedMb;
    double totalMb;       // 0 if unknown
    double percent;       // 0..100, -1 if unknown
    double overallMb;     // cumulative across all shards
    double overallTotalMb;
    double overallPercent;
};

using ProgressCallback = std::function<void(const DownloadProgress&)>;

class MoEDownloader {
public:
    MoEDownloader();

    // Set a proxy URL (e.g. "http://127.0.0.1:7897"). Empty = no proxy.
    void setProxy(const std::string& proxy) { proxy_ = proxy; }

    // Set timeout per shard (seconds, 0 = no limit).
    void setTimeout(int seconds) { timeoutSec_ = seconds; }

    // Cancel an in-progress download (thread-safe).
    void cancel() { cancelled_ = true; }
    bool isCancelled() const { return cancelled_; }

    // Download all shards of `entry` into `outDir`.
    // Progress is reported via the callback. Returns true if all shards
    // downloaded and verified (or already present and valid).
    bool downloadModel(const ModelEntry& entry,
                       const std::string& outDir,
                       ProgressCallback cb = nullptr);

    // Verify that all shard files exist and match their SHA-256 (if set).
    // Returns true if all shards present and valid.
    bool verifyModel(const ModelEntry& entry, const std::string& outDir);

    // Check if a model is already fully downloaded in outDir.
    bool isModelPresent(const ModelEntry& entry, const std::string& outDir);

private:
    // Download a single shard with resume support. Returns bytes downloaded
    // (0 if already complete).
    int64_t downloadShard(const ModelShard& shard,
                          const std::string& outPath,
                          double shardTotalMb,
                          int shardIndex, int shardCount,
                          double& overallMb, double overallTotalMb,
                          ProgressCallback cb);

    std::string proxy_;
    int         timeoutSec_ = 0;
    std::atomic<bool> cancelled_{false};

    static int64_t fileSize(const std::string& path);
    static std::string sha256File(const std::string& path);
    static bool sha256Check(const std::string& path, const std::string& expected);
};

} // namespace closecrab::moe
