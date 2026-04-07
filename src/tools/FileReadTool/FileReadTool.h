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

        if (!fs::exists(path)) {
            return ToolResult::fail("File not found: " + path);
        }
        if (fs::is_directory(path)) {
            return ToolResult::fail("Path is a directory, not a file: " + path);
        }

        auto fileSize = fs::file_size(path);
        if (fileSize > 1024 * 1024 * 100) { // 100MB limit
            return ToolResult::fail("File too large: " + std::to_string(fileSize) + " bytes");
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            return ToolResult::fail("Cannot open file: " + path);
        }

        int offset = input.value("offset", 0);
        int limit = input.value("limit", 2000);

        std::string result;
        std::string line;
        int lineNum = 0;
        int linesRead = 0;
        int totalLines = 0;

        // Count total lines first (for metadata)
        while (std::getline(file, line)) totalLines++;
        file.clear();
        file.seekg(0);

        // Read with offset and limit
        while (std::getline(file, line)) {
            if (lineNum >= offset && linesRead < limit) {
                result += std::to_string(lineNum + 1) + "\t" + line + "\n";
                linesRead++;
            }
            lineNum++;
            if (linesRead >= limit) break;
        }

        nlohmann::json data = {
            {"filePath", path},
            {"numLines", linesRead},
            {"startLine", offset + 1},
            {"totalLines", totalLines}
        };

        return ToolResult::ok(ensureUtf8(result), data);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Read " + input.value("file_path", "file");
    }
};

} // namespace closecrab
