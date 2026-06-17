#pragma once

#include "../Tool.h"
#include "../../core/FileStateCache.h"
#include "../../core/MmapCache.h"
#include "../../utils/StringUtils.h"
#include "../../utils/PdfUtils.h"
#include "../../utils/ImageUtils.h"
#include "../../utils/FileSecurityUtils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <mutex>

namespace closecrab {

class FileReadTool : public Tool {
public:
    std::string getName() const override { return "Read"; }
    std::string getDescription() const override {
        return "Read file contents. Supports text files, with optional line offset and limit.";
    }
    std::string getCategory() const override { return "file"; }
    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"file_path", {{"type", "string"}, {"description", "Absolute path to the file"}}},
                {"offset", {{"type", "integer"}, {"description", "Line number to start from (0-based)"}}},
                {"limit", {{"type", "integer"}, {"description", "Max lines to read"}}}
            }},
            {"required", {"file_path"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
      std::string path = input["file_path"].get<std::string>();

        // Unicode-safe path: interpret the incoming string as UTF-8 and build a
        // native (wide on Windows) path. A narrow std::string path goes through
        // the ANSI code page on MSVC, which throws on CJK filenames ("需求.txt").
        fs::path fsPath;
        try {
            fsPath = fs::u8path(path);
        } catch (...) {
            fsPath = fs::path(path);
        }

        std::error_code ec;
        if (!fs::exists(fsPath, ec) || ec) {
            return ToolResult::fail("File not found: " + path);
        }
        if (fs::is_directory(fsPath, ec)) {
            return ToolResult::fail("Path is a directory, not a file: " + path);
        }

        // Binary file detection: check file extension and first bytes to prevent
        // crashes from reading binary files as text (docx/pdf/jpg/exe/dll/etc).
        // This prevents the heap corruption crash when large binary files are
        // interpreted as text, producing garbage UTF-8 sequences.
        std::string ext = fsPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Common binary extensions
        static const std::set<std::string> binaryExts = {
         ".exe", ".dll", ".so", ".dylib", ".bin", ".dat", ".db", ".sqlite",
            ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".ico", ".webp", ".svg",
         ".pdf", ".docx", ".xlsx", ".pptx", ".doc", ".xls", ".ppt",
            ".zip", ".tar", ".gz", ".7z", ".rar", ".bz2",
        ".mp3", ".mp4", ".avi", ".mkv", ".wav", ".flac",
            ".woff", ".woff2", ".ttf", ".otf", ".eot",
            ".pyc", ".pyo", ".class", ".o", ".obj", ".a", ".lib"
      };

      if (binaryExts.count(ext) > 0) {
            return ToolResult::fail(
                "Cannot read binary file: " + path + "\n" +
            "File type: " + ext + "\n" +
              "Binary files (.docx, .pdf, .jpg, etc.) cannot be read as text.\n" +
                "For images, use ImageInput tool. For documents, convert to plain text first."
            );
        }

      // Additional binary check: read first 512 bytes and look for null bytes
        // or high percentage of non-text bytes (catches files without known extensions)
        try {
            std::ifstream testFile(fsPath, std::ios::binary);
            if (testFile.is_open()) {
              char buffer[512];
              testFile.read(buffer, sizeof(buffer));
                std::streamsize bytesRead = testFile.gcount();

           if (bytesRead > 0) {
                    int nullBytes = 0;
                    int controlBytes = 0;
                    for (std::streamsize i = 0; i < bytesRead; i++) {
                     unsigned char c = static_cast<unsigned char>(buffer[i]);
             if (c == 0) nullBytes++;
                   // Control characters except tab, newline, carriage return
                   if (c < 32 && c != 9 && c != 10 && c != 13) controlBytes++;
                    }

                  // If >10% null bytes or >30% control chars, likely binary
          if (nullBytes > bytesRead / 10 || controlBytes > bytesRead / 3) {
                      return ToolResult::fail(
                     "Cannot read binary file: " + path + "\n" +
          "File appears to contain binary data (detected null bytes or excessive control characters).\n" +
                "This file cannot be safely read as text."
                        );
                    }
             }
            }
     } catch (...) {
         // If binary detection fails, continue with normal read
        // (better to try than block legitimate files)
        }

        int offset = jsonInt(input, "offset", 0);
        int limit = jsonInt(input, "limit", 2000);
        if (limit <= 0) limit = 2000;
        bool hasExplicitLimit = input.contains("limit") && !input["limit"].is_null();

        // Dedup (claude-code readFileState): if we already read this exact range
        // and the file hasn't changed on disk, return a stub instead of re-sending
        // the full content. Saves ~18% of cache_creation tokens (measured by claude-code).
        {
            std::lock_guard<std::mutex> lock(dedupMutex_);
            std::string dedupKey = path + ":" + std::to_string(offset) + ":" + std::to_string(limit);
            auto it = readState_.find(dedupKey);
            if (it != readState_.end()) {
                try {
                    auto mtime = fs::last_write_time(fsPath, ec);
                    if (!ec && mtime == it->second.mtime) {
                        nlohmann::json data = {
                            {"filePath", path}, {"numLines", it->second.numLines},
                            {"startLine", offset + 1}, {"totalLines", it->second.totalLines},
                            {"truncated", false}, {"dedup", true}
                        };
                        return ToolResult::ok(
                            "[file unchanged since last read — content already in context]", data);
                    }
                } catch (...) {}
                readState_.erase(it);
            }
        }

        // Byte safety cap so an unbounded read of a huge file can't blow up memory.
        // Only applies when no explicit limit was requested.
        constexpr size_t BYTE_CAP = 1024 * 1024; // 1MB of returned text

        // --- Mmap-cached read path ---
        // Try to get the file from the mmap cache. On hit with unchanged mtime,
        // this avoids the open/read/close syscall overhead entirely.
        auto& mmapCache = MmapCache::getInstance();
        const char* mappedData = nullptr;
        size_t mappedSize = 0;

        bool mmapOk = mmapCache.get(path, mappedData, mappedSize);
        if (!mmapOk) {
            mmapOk = mmapCache.map(path, mappedData, mappedSize);
        }

        std::string result;
        int lineNum = 0;
        int linesRead = 0;
        bool truncatedByBytes = false;

        if (mmapOk && mappedData != nullptr && mappedSize > 0) {
            // Iterate over the memory-mapped content line by line
            const char* pos = mappedData;
            const char* end = mappedData + mappedSize;

            while (pos < end) {
                // Find end of line
                const char* lineEnd = static_cast<const char*>(
                    std::memchr(pos, '\n', static_cast<size_t>(end - pos)));
                if (!lineEnd) lineEnd = end;

                if (lineNum >= offset && linesRead < limit) {
                    size_t lineLen = static_cast<size_t>(lineEnd - pos);
                    // Strip trailing CR for CRLF
                    if (lineLen > 0 && pos[lineLen - 1] == '\r') lineLen--;

                    result += std::to_string(lineNum + 1);
                    result += '\t';
                    result.append(pos, lineLen);
                    result += '\n';
                    linesRead++;

                    if (!hasExplicitLimit && result.size() > BYTE_CAP) {
                        truncatedByBytes = true;
                        lineNum++;
                        break;
                    }
                }
                lineNum++;
                pos = (lineEnd < end) ? lineEnd + 1 : end;

                // Stop scanning for total-line count once we've read our window plus a
                // small lookahead — avoids walking the rest of a multi-GB file.
                if (linesRead >= limit && lineNum > offset + limit + 1000) break;
            }
        } else if (mmapOk && (mappedData == nullptr || mappedSize == 0)) {
            // Empty file — nothing to read
        } else {
            // Mmap failed (e.g. file too large for cache, or permission issue).
            // Fall back to traditional ifstream read.
            std::ifstream file(fsPath, std::ios::binary);
            if (!file.is_open()) {
                return ToolResult::fail("Cannot open file: " + path);
            }

            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (lineNum >= offset && linesRead < limit) {
                    result += std::to_string(lineNum + 1) + "\t" + line + "\n";
                    linesRead++;
                    if (!hasExplicitLimit && result.size() > BYTE_CAP) {
                        truncatedByBytes = true;
                        lineNum++;
                        break;
                    }
                }
                lineNum++;
                if (linesRead >= limit && lineNum > offset + limit + 1000) break;
            }
        }

        int totalLines = lineNum;

        // Release the memory-mapped view now that content is copied into result.
        // On Windows an active file mapping prevents the file from being truncated,
        // overwritten or deleted by any process (editors/compilers and even
        // CloseCrab own write/edit tools). Data is already copied, so unmap now.
        mmapCache.invalidate(path);

        // Append a truncation note so the model knows there's more and how to get it.
        bool truncated = truncatedByBytes || (linesRead >= limit && totalLines > offset + linesRead);
        if (truncated) {
            int nextOffset = offset + linesRead;
            result += "\n[Truncated: showed lines " + std::to_string(offset + 1) + "-" +
                      std::to_string(offset + linesRead) + ". File has more content. " +
                      "Read more with offset=" + std::to_string(nextOffset) +
                      (truncatedByBytes ? " (stopped at 1MB)." : ".") + "]\n";
        }

        nlohmann::json data = {
            {"filePath", path},
            {"numLines", linesRead},
            {"startLine", offset + 1},
            {"totalLines", totalLines},
            {"truncated", truncated}
        };

        // Store dedup state for this read
        {
            std::lock_guard<std::mutex> lock(dedupMutex_);
            std::string dedupKey = path + ":" + std::to_string(offset) + ":" + std::to_string(limit);
            DedupEntry entry;
            try { entry.mtime = fs::last_write_time(fsPath, ec); } catch (...) {}
            entry.numLines = linesRead;
            entry.totalLines = totalLines;
            readState_[dedupKey] = entry;
        }

        // §6: Update session-level readFileState for write-before-read enforcement.
        // JackProAi FileState semantics (utils/fileStateCache.ts:4-15):
        //   timestamp - file mtime at read time
        //   offset/limit - set for range reads (used for read dedup, NOT to block writes)
        //   isPartialView - ONLY true for auto-injected content differing from disk;
        //                   a normal range Read is NOT partial.
        // We store contentHash+size (instead of JackProAi's raw `content`) for the
        // Windows mtime-false-positive fallback (StringUtils fileContentHash).
        if (ctx.readFileState) {
            ToolContext::ReadState rs;
            try {
                auto mtime = fs::last_write_time(fsPath, ec);
                rs.mtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    mtime.time_since_epoch()).count();
            } catch (...) { rs.mtimeMs = 0; }
            auto [h, sz] = fileContentHash(fsPath);
            rs.contentHash = h;
            rs.contentSize = sz;
            rs.hasOffset = (offset > 0);
            rs.hasLimit = hasExplicitLimit;
            rs.isPartialView = false;  // FileReadTool always shows real disk content
            (*ctx.readFileState)[normalizePathKey(path)] = rs;
        }

        return ToolResult::ok(ensureUtf8(result), data);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Read " + input.value("file_path", "file");
    }

private:
    struct DedupEntry {
        std::filesystem::file_time_type mtime;
        int numLines = 0;
        int totalLines = 0;
    };
    mutable std::mutex dedupMutex_;
    mutable std::map<std::string, DedupEntry> readState_;
};

} // namespace closecrab
