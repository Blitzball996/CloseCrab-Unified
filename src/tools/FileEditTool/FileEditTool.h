#pragma once

#include "../Tool.h"
#include "../../core/FileStateCache.h"
#include "../../utils/StringUtils.h"
#include "../../utils/DiffRender.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace closecrab {

class FileEditTool : public Tool {
public:
    std::string getName() const override { return "Edit"; }
    std::string getDescription() const override {
        return "Edit a file by replacing an exact string match with new content, "
               "or by specifying line_start/line_end to replace a line range. "
               "The old_string must be unique in the file.";
    }
    std::string getCategory() const override { return "file"; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"file_path", {{"type", "string"}, {"description", "Absolute path to the file"}}},
                {"old_string", {{"type", "string"}, {"description", "Exact string to find and replace"}}},
                {"new_string", {{"type", "string"}, {"description", "Replacement string"}}},
                {"replace_all", {{"type", "boolean"}, {"description", "Replace all occurrences (default false)"}}},
                {"line_start", {{"type", "integer"}, {"description", "Start line number (alternative to old_string)"}}},
                {"line_end", {{"type", "integer"}, {"description", "End line number (inclusive)"}}}
            }},
            {"required", {"file_path", "new_string"}}
        };
    }

    ValidationResult validateInput(const nlohmann::json& input) const override {
        auto base = Tool::validateInput(input);
        if (!base.valid) return base;

        // Line-number mode doesn't need old_string
        if (input.contains("line_start") && input.contains("line_end")) {
            return ValidationResult::ok();
        }

        if (!input.contains("old_string")) {
            return ValidationResult::fail("Either old_string or line_start/line_end is required");
        }

        if (input["old_string"].get<std::string>() == input["new_string"].get<std::string>()) {
            return ValidationResult::fail("old_string and new_string are identical");
        }
        return ValidationResult::ok();
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
        std::string path = input["file_path"].get<std::string>();
        std::string newStr = input["new_string"].get<std::string>();

        // UTF-8-safe path (handles CJK filenames like "需求.txt" on Windows)
        fs::path fsPath = utf8Path(path);

        std::error_code ec;
        if (!fs::exists(fsPath, ec) || ec) {
            return ToolResult::fail("File not found: " + path);
        }

        // §6: Read-before-write enforcement (JackProAi FileEditTool.ts:275-294)
        if (ctx.readFileState) {
            std::string key = normalizePathKey(path);
            auto it = ctx.readFileState->find(key);
            if (it == ctx.readFileState->end() || it->second.isPartialView) {
                return ToolResult::fail(
                    "File has not been read yet. You MUST use the Read tool to read "
                    "the file before editing it.");
            }
            auto currentMtime = fs::last_write_time(fsPath, ec);
            if (!ec) {
                int64_t currentMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentMtime.time_since_epoch()).count();
                if (currentMs > it->second.mtimeMs) {
                    // mtime false-positive fallback via content hash (JackProAi).
                    auto [curHash, curSize] = fileContentHash(fsPath);
                    bool contentMatches = (curSize == it->second.contentSize &&
                                           curHash == it->second.contentHash &&
                                           it->second.contentSize > 0);
                    if (!contentMatches) {
                        return ToolResult::fail(
                            "File has been modified since you last read it (by user or another process). "
                            "Read it again before attempting to edit.");
                    }
                }
            }
        }

        // Read file
        std::ifstream inFile(fsPath, std::ios::binary);
        if (!inFile.is_open()) {
            return ToolResult::fail(
                "Cannot open file: " + path +
                " (likely locked by editor/Live Server/another process). "
                "DO NOT delete the file. Ask the user to close the application, then retry.");
        }
        std::string content((std::istreambuf_iterator<char>(inFile)), {});
        inFile.close();

        // Line-number mode: replace lines line_start to line_end with new_string
        if (input.contains("line_start") && input.contains("line_end")) {
            int lineStart = jsonInt(input, "line_start", 0);
            int lineEnd = jsonInt(input, "line_end", 0);

            std::vector<std::string> lines;
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line)) lines.push_back(line);

            if (lineStart < 1 || lineEnd > (int)lines.size() || lineStart > lineEnd) {
                return ToolResult::fail("Invalid line range: " + std::to_string(lineStart) +
                    "-" + std::to_string(lineEnd) + " (file has " +
                    std::to_string(lines.size()) + " lines)");
            }

            std::string modified;
            for (int i = 0; i < (int)lines.size(); i++) {
                if (i + 1 == lineStart) {
                    modified += newStr;
                    if (!newStr.empty() && newStr.back() != '\n') modified += "\n";
                    i = lineEnd - 1; // Skip to end of range
                } else {
                    modified += lines[i] + "\n";
                }
            }

            std::ofstream outFile(fsPath, std::ios::binary);
            if (!outFile.is_open()) {
                return ToolResult::fail(
                    "Cannot write to file: " + path +
                    " (likely locked by editor/Live Server/another process). "
                    "DO NOT delete the file. Ask the user to close the application, then retry.");
            }
            outFile << modified;
            outFile.close();

            FileStateCache::getInstance().invalidate(path);
            // Update readFileState after successful write
            if (ctx.readFileState) {
                ToolContext::ReadState rs;
                std::error_code wec;
                try {
                    auto mtime = fs::last_write_time(fsPath, wec);
                    rs.mtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        mtime.time_since_epoch()).count();
                } catch (...) { rs.mtimeMs = 0; }
                auto [h, sz] = fileContentHash(fsPath);
                rs.contentHash = h; rs.contentSize = sz;
                rs.isPartialView = false;
                (*ctx.readFileState)[normalizePathKey(path)] = rs;
            }
            // 3.1: attach a compact diff for the UI (NOT sent to the model — stays
            // out of the tool_result text, only in .data, per the §7 token rule).
            nlohmann::json okData;
            okData["diff"] = DiffRender::build(content, modified);
            okData["filePath"] = path;
            return ToolResult::ok("Replaced lines " + std::to_string(lineStart) + "-" +
                std::to_string(lineEnd) + " in " + path, okData);
        }

        // old_string mode
        std::string oldStr = input["old_string"].get<std::string>();
        bool replaceAll = jsonBool(input, "replace_all", false);

        // Count occurrences
        size_t count = 0;
        size_t pos = 0;
        while ((pos = content.find(oldStr, pos)) != std::string::npos) {
            count++;
            pos += oldStr.size();
        }

        if (count == 0) {
            return ToolResult::fail("old_string not found in file. Make sure it matches exactly.");
        }

        if (count > 1 && !replaceAll) {
            // Show line numbers of each occurrence to help AI provide more context
            std::string locations;
            size_t searchPos = 0;
            int lineNum = 1;
            int occNum = 0;
            for (size_t i = 0; i < content.size(); i++) {
                if (content[i] == '\n') lineNum++;
                if (i == content.find(oldStr, searchPos)) {
                    occNum++;
                    locations += " occurrence " + std::to_string(occNum) + " at line " + std::to_string(lineNum) + ";";
                    searchPos = i + oldStr.size();
                }
            }
            return ToolResult::fail(
                "old_string found " + std::to_string(count) + " times (" + locations +
                "). Include more surrounding lines to make it unique, or set replace_all=true.");
        }

        // Perform replacement
        std::string modified = content;
        if (replaceAll) {
            pos = 0;
            while ((pos = modified.find(oldStr, pos)) != std::string::npos) {
                modified.replace(pos, oldStr.size(), newStr);
                pos += newStr.size();
            }
        } else {
            pos = modified.find(oldStr);
            modified.replace(pos, oldStr.size(), newStr);
        }

        // Write back
        std::ofstream outFile(fsPath, std::ios::binary);
        if (!outFile.is_open()) {
            return ToolResult::fail("Cannot write to file: " + path);
        }
        outFile << modified;
        outFile.close();

        // claude-code: tool_result is just a short confirmation.
        // "The file {path} has been updated successfully."
        // Do NOT include diff content — it bloats the next API request.
        std::string msg = replaceAll
            ? "Replaced " + std::to_string(count) + " occurrences in " + path
            : "The file " + path + " has been updated successfully.";

        FileStateCache::getInstance().invalidate(path);
        // Update readFileState after successful write
        if (ctx.readFileState) {
            ToolContext::ReadState rs;
            std::error_code wec;
            try {
                auto mtime = fs::last_write_time(fsPath, wec);
                rs.mtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    mtime.time_since_epoch()).count();
            } catch (...) { rs.mtimeMs = 0; }
            auto [h, sz] = fileContentHash(fsPath);
            rs.contentHash = h; rs.contentSize = sz;
            rs.isPartialView = false;
            (*ctx.readFileState)[normalizePathKey(path)] = rs;
        }
        nlohmann::json okData;
        okData["diff"] = DiffRender::build(content, modified);
        okData["filePath"] = path;
        return ToolResult::ok(msg, okData);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Edit " + input.value("file_path", "file");
    }
};

} // namespace closecrab
