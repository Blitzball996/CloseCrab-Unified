#pragma once

#include "../Tool.h"
#include "../../core/FileStateCache.h"
#include "../../utils/StringUtils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
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

        int offset = input.value("offset", 0);
        int limit = input.value("limit", 2000);
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

        std::ifstream file(fsPath, std::ios::binary);
        if (!file.is_open()) {
            return ToolResult::fail("Cannot open file: " + path);
        }

        std::string result;
        std::string line;
        int lineNum = 0;
        int linesRead = 0;
        bool truncatedByBytes = false;

        while (std::getline(file, line)) {
            // Strip a trailing CR so CRLF files don't leave stray \r in output.
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
            // Stop scanning for total-line count once we've read our window plus a
            // small lookahead — avoids walking the rest of a multi-GB file.
            if (linesRead >= limit && lineNum > offset + limit + 1000) break;
        }
        int totalLines = lineNum;

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
