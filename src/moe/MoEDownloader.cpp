// MoEDownloader.cpp — multi-shard model downloader with resume + SHA-256.
#include "MoEDownloader.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <wincrypt.h>
#  pragma comment(lib, "advapi32.lib")
#else
#  include <openssl/sha.h>
#endif

namespace fs = std::filesystem;
namespace closecrab::moe {

MoEDownloader::MoEDownloader() = default;

// ---- file helpers ----------------------------------------------------------

int64_t MoEDownloader::fileSize(const std::string& path) {
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    return ec ? 0 : static_cast<int64_t>(sz);
}

// SHA-256 of a file. Windows: CryptAPI. Linux/macOS: OpenSSL.
std::string MoEDownloader::sha256File(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};

#if defined(_WIN32)
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL,
                               CRYPT_VERIFYCONTEXT)) return {};
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0); return {};
    }
    std::array<char, 65536> buf{};
    while (f) {
        f.read(buf.data(), buf.size());
        auto n = static_cast<DWORD>(f.gcount());
        if (n == 0) break;
        if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(buf.data()), n, 0))
            break;
    }
    DWORD hashLen = 32;
    std::array<unsigned char, 32> raw{};
    std::string result;
    if (CryptGetHashParam(hHash, HP_HASHVAL, raw.data(), &hashLen, 0)) {
        char hex[3];
        for (int i = 0; i < 32; ++i) {
            std::snprintf(hex, sizeof hex, "%02x", raw[i]);
            result += hex;
        }
    }
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return result;
#else
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    std::array<char, 65536> buf{};
    while (f) {
        f.read(buf.data(), buf.size());
        auto n = static_cast<size_t>(f.gcount());
        if (n == 0) break;
        SHA256_Update(&ctx, buf.data(), n);
    }
    std::array<unsigned char, SHA256_DIGEST_LENGTH> raw{};
    SHA256_Final(raw.data(), &ctx);
    char hex[3];
    std::string result;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        std::snprintf(hex, sizeof hex, "%02x", raw[i]);
        result += hex;
    }
    return result;
#endif
}

bool MoEDownloader::sha256Check(const std::string& path, const std::string& expected) {
    if (expected.empty()) return true;  // no checksum = skip
    std::string actual = sha256File(path);
    // Compare case-insensitive
    std::string a = actual, e = expected;
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return a == e;
}

// ---- model presence / verify -----------------------------------------------

bool MoEDownloader::isModelPresent(const ModelEntry& entry, const std::string& outDir) {
    for (auto& s : entry.shards) {
        std::string name = s.hfFile.substr(s.hfFile.find_last_of('/') + 1);
        if (!fs::exists(fs::path(outDir) / name)) return false;
    }
    return true;
}

bool MoEDownloader::verifyModel(const ModelEntry& entry, const std::string& outDir) {
    for (auto& s : entry.shards) {
        std::string name = s.hfFile.substr(s.hfFile.find_last_of('/') + 1);
        std::string p = (fs::path(outDir) / name).string();
        if (!fs::exists(p)) return false;
        if (!s.sha256.empty() && !sha256Check(p, s.sha256)) return false;
    }
    return true;
}

// ---- single shard download with resume -------------------------------------

int64_t MoEDownloader::downloadShard(const ModelShard& shard,
                                      const std::string& outPath,
                                      double shardTotalMb,
                                      int shardIndex, int shardCount,
                                      double& overallMb, double overallTotalMb,
                                      ProgressCallback cb) {
    // Build curl command with resume (-C -), retry, and progress to stderr.
    // We parse progress from stderr in real time is complex; instead we poll
    // the output file size periodically and report it.
    std::string url = "https://huggingface.co/" + shard.hfRepo + "/resolve/main/" + shard.hfFile;
    std::string tmpPath = outPath + ".tmp";

    // Remove .tmp if a stale partial from a different run exists but is full-size
    int64_t existing = fileSize(tmpPath);

    std::ostringstream cmd;
    cmd << "curl -L --retry 5 --retry-delay 3 --retry-all-errors -C - "
        << "-o \"" << tmpPath << "\" ";
    if (!proxy_.empty()) cmd << "--proxy \"" << proxy_ << "\" ";
    if (timeoutSec_ > 0) cmd << "--max-time " << timeoutSec_ << " ";
    cmd << "\"" << url << "\"";
    std::string cmdStr = cmd.str();

    spdlog::info("MoE download: shard {}/{} -> {} ({:.1f} MB)",
                 shardIndex + 1, shardCount, shard.hfFile, shardTotalMb);

    // Run curl in background, polling file size for progress.
    // On Windows _popen captures stdout; curl writes progress to stderr
    // with --progress-bar. We use -# (progress bar) and parse it, or
    // simply poll file size.
#if defined(_WIN32)
    FILE* fp = _popen(cmdStr.c_str(), "r");
#else
    FILE* fp = popen(cmdStr.c_str(), "r");
#endif
    if (!fp) {
        spdlog::error("MoE download: failed to start curl");
        return 0;
    }

    // Poll file size for progress while curl runs.
    while (true) {
#if defined(_WIN32)
        // Non-blocking: check if process is still running by attempting read.
        // _popen doesn't give a clean poll; instead, sleep and check file size.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        int64_t cur = fileSize(tmpPath);
        double dlMb = static_cast<double>(cur) / (1024.0 * 1024.0);
        double pct = shardTotalMb > 0 ? std::min(100.0, dlMb / shardTotalMb * 100.0) : -1;
        overallMb += 0;  // updated below
        if (cb) {
            DownloadProgress p{};
            p.shardIndex = shardIndex;
            p.shardCount = shardCount;
            p.downloadedMb = dlMb;
            p.totalMb = shardTotalMb;
            p.percent = pct;
            p.overallMb = overallMb + dlMb;
            p.overallTotalMb = overallTotalMb;
            p.overallPercent = overallTotalMb > 0
                ? std::min(100.0, p.overallMb / overallTotalMb * 100.0) : -1;
            cb(p);
        }
        if (cancelled_) break;
        // Check if curl is done by reading stdout (non-blocking would be ideal).
        char buf[256];
        if (fgets(buf, sizeof buf, fp)) {
            // curl --progress-bar wrote something; keep going
        } else {
            // EOF = curl finished
            break;
        }
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        int64_t cur = fileSize(tmpPath);
        double dlMb = static_cast<double>(cur) / (1024.0 * 1024.0);
        double pct = shardTotalMb > 0 ? std::min(100.0, dlMb / shardTotalMb * 100.0) : -1;
        if (cb) {
            DownloadProgress p{};
            p.shardIndex = shardIndex;
            p.shardCount = shardCount;
            p.downloadedMb = dlMb;
            p.totalMb = shardTotalMb;
            p.percent = pct;
            p.overallMb = overallMb + dlMb;
            p.overallTotalMb = overallTotalMb;
            p.overallPercent = overallTotalMb > 0
                ? std::min(100.0, p.overallMb / overallTotalMb * 100.0) : -1;
            cb(p);
        }
        if (cancelled_) break;
        // Check if process still running
        char buf[256];
        if (fgets(buf, sizeof buf, fp)) {
        } else {
            break;
        }
#endif
    }

#if defined(_WIN32)
    int rc = _pclose(fp);
#else
    int rc = pclose(fp);
#endif

    if (cancelled_) {
        spdlog::warn("MoE download: cancelled");
        return 0;
    }
    if (rc != 0) {
        spdlog::error("MoE download: curl failed (rc={})", rc);
        return 0;
    }

    // Move .tmp to final name
    std::error_code ec;
    fs::rename(tmpPath, outPath, ec);
    if (ec) {
        // rename may fail across volumes; copy instead
        fs::copy_file(tmpPath, outPath, ec);
        fs::remove(tmpPath, ec);
    }

    int64_t finalSz = fileSize(outPath);
    if (cb) {
        DownloadProgress p{};
        p.shardIndex = shardIndex;
        p.shardCount = shardCount;
        p.downloadedMb = static_cast<double>(finalSz) / (1024.0 * 1024.0);
        p.totalMb = shardTotalMb;
        p.percent = 100.0;
        overallMb += p.downloadedMb;
        p.overallMb = overallMb;
        p.overallTotalMb = overallTotalMb;
        p.overallPercent = overallTotalMb > 0
            ? std::min(100.0, overallMb / overallTotalMb * 100.0) : -1;
        cb(p);
    }
    return finalSz;
}

// ---- full model download ----------------------------------------------------

bool MoEDownloader::downloadModel(const ModelEntry& entry,
                                   const std::string& outDir,
                                   ProgressCallback cb) {
    cancelled_ = false;
    std::error_code ec;
    fs::create_directories(outDir, ec);

    int nShards = static_cast<int>(entry.shards.size());
    double overallTotalMb = entry.downloadGb * 1024.0;
    double overallMb = 0;

    for (int i = 0; i < nShards; ++i) {
        if (cancelled_) return false;
        auto& shard = entry.shards[i];
        std::string name = shard.hfFile.substr(shard.hfFile.find_last_of('/') + 1);
        std::string outPath = (fs::path(outDir) / name).string();

        // Skip if already present and verified
        if (fs::exists(outPath) && sha256Check(outPath, shard.sha256)) {
            double existingMb = static_cast<double>(fileSize(outPath)) / (1024.0 * 1024.0);
            overallMb += existingMb;
            if (cb) {
                DownloadProgress p{};
                p.shardIndex = i;
                p.shardCount = nShards;
                p.downloadedMb = existingMb;
                p.totalMb = shard.sizeGb * 1024.0;
                p.percent = 100.0;
                p.overallMb = overallMb;
                p.overallTotalMb = overallTotalMb;
                p.overallPercent = overallTotalMb > 0
                    ? std::min(100.0, overallMb / overallTotalMb * 100.0) : -1;
                cb(p);
            }
            spdlog::info("MoE download: shard {}/{} already present", i + 1, nShards);
            continue;
        }

        int64_t sz = downloadShard(shard, outPath, shard.sizeGb * 1024.0,
                                    i, nShards, overallMb, overallTotalMb, cb);
        if (sz == 0 && !fs::exists(outPath)) {
            spdlog::error("MoE download: shard {} failed", i + 1);
            return false;
        }
        if (!shard.sha256.empty() && !sha256Check(outPath, shard.sha256)) {
            spdlog::error("MoE download: shard {} SHA-256 mismatch", i + 1);
            return false;
        }
    }

    spdlog::info("MoE download: all {} shards complete", nShards);
    return true;
}

} // namespace closecrab::moe
