#pragma once

#include "../Tool.h"
#include "../../core/FileStateCache.h"
#include "../../utils/StringUtils.h"
#include "../../utils/DiffRender.h"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace closecrab {

class FileWriteTool : public Tool {
public:
    std::string getName() const override { return "Write"; }
    std::string getDescription() const override {
        return "Create a new file or completely overwrite an existing file with new content.";
    }
    std::string getCategory() const override { return "file"; }
    bool isDestructive() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"file_path", {{"type", "string"}, {"description", "Absolute path to the file"}}},
                {"content", {{"type", "string"}, {"description", "Content to write"}}}
            }},
            {"required", {"file_path", "content"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
        std::string path = input["file_path"].get<std::string>();
        std::string content = input["content"].get<std::string>();

        // UTF-8-safe path (handles CJK filenames like "需求.txt" on Windows)
        fs::path fsPath = utf8Path(path);

        // §6: Read-before-write enforcement (JackProAi FileWriteTool.ts:198-294)
        std::error_code ec;
        if (fs::exists(fsPath, ec) && !ec) {
            if (ctx.readFileState) {
                std::string key = normalizePathKey(path);
                auto it = ctx.readFileState->find(key);
                if (it == ctx.readFileState->end() || it->second.isPartialView) {
                    return ToolResult::fail(
                        "File has not been read yet. You MUST use the Read tool to read "
                        "the file before writing to it.");
                }
                auto currentMtime = fs::last_write_time(fsPath, ec);
                if (!ec) {
                    int64_t currentMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        currentMtime.time_since_epoch()).count();
                    if (currentMs > it->second.mtimeMs) {
                        // mtime changed — but on Windows cloud-sync/antivirus can bump
                        // mtime without content change. Fall back to content hash
                        // comparison to avoid false positives (JackProAi
                        // FileWriteTool.ts:282-294).
                        auto [curHash, curSize] = fileContentHash(fsPath);
                        bool contentMatches = (curSize == it->second.contentSize &&
                                               curHash == it->second.contentHash &&
                                               it->second.contentSize > 0);
                        if (!contentMatches) {
                            return ToolResult::fail(
                                "File has been modified since you last read it (by user or another process). "
                                "Read it again before attempting to write.");
                        }
                    }
                }
            }
        }

        // Ensure parent directory exists
        fs::path parentDir = fsPath.parent_path();
        if (!parentDir.empty() && !fs::exists(parentDir, ec)) {
            fs::create_directories(parentDir, ec);
        }

        // 3.1: capture old content (if any) so we can show a diff of the overwrite.
        std::string oldContent;
        bool existed = false;
        {
            std::ifstream inFile(fsPath, std::ios::binary);
            if (inFile.is_open()) {
                oldContent.assign((std::istreambuf_iterator<char>(inFile)), {});
                existed = true;
            }
        }

        std::ofstream file(fsPath, std::ios::binary);
        if (!file.is_open()) {
            return ToolResult::fail(
                "Cannot open file for writing: " + path +
                " (likely locked by editor/Live Server/another process). "
                "DO NOT delete the file. Ask the user to close the application, then retry.");
        }

        file << content;
        file.close();

        if (file.fail()) {
            return ToolResult::fail("Failed to write file: " + path);
        }

        // claude-code mapToolResultToToolResultBlockParam: the API-facing
        // tool_result is JUST a short confirmation. Including file content
        // here caused the next API request to be 68KB+ → proxy timeout →
        // infinite retry loop. The UI can show a preview separately.
        std::string msg = "File created successfully at: " + path +
                          " (" + std::to_string(content.size()) + " bytes)";

        FileStateCache::getInstance().invalidate(path);

        // Update readFileState so subsequent writes don't fail the mtime check
        if (ctx.readFileState) {
            ToolContext::ReadState rs;
            std::error_code wec;
            try {
                auto mtime = fs::last_write_time(fsPath, wec);
                rs.mtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    mtime.time_since_epoch()).count();
            } catch (...) { rs.mtimeMs = 0; }
            auto [h, sz] = fileContentHash(fsPath);
            rs.contentHash = h;
            rs.contentSize = sz;
            rs.hasOffset = false;
            rs.hasLimit = false;
            rs.isPartialView = false;
            (*ctx.readFileState)[normalizePathKey(path)] = rs;
        }

        // 3.1: attach a diff for the UI. For an overwrite, old→new; for a brand
        // new file, show the content as all-added (capped by DiffRender).
        nlohmann::json okData;
        okData["diff"] = DiffRender::build(existed ? oldContent : std::string(), content);
        okData["filePath"] = path;
        okData["created"] = !existed;
        return ToolResult::ok(msg, okData);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Write " + input.value("file_path", "file");
    }
};

} // namespace closecrab
