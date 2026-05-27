#pragma once

#include "../Tool.h"
#include "../../core/FileStateCache.h"
#include "../../utils/StringUtils.h"
#include <fstream>
#include <sstream>
#include <filesystem>

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

        // Whether the model explicitly scoped the read. JackProAi only applies a
        // size cap when reading the WHOLE file (no limit); an explicit limit reads
        // that range regardless of file size.
        bool hasExplicitLimit = input.contains("limit") && !input["limit"].is_null();
        int offset = input.value("offset", 0);
        int limit = input.value("limit", 2000);
        if (limit <= 0) limit = 2000;

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

        return ToolResult::ok(ensureUtf8(result), data);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Read " + input.value("file_path", "file");
    }
};

} // namespace closecrab
