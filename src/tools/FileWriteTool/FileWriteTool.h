#pragma once

#include "../Tool.h"
#include "../../core/FileStateCache.h"
#include "../../utils/StringUtils.h"
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

        // Ensure parent directory exists
        std::error_code ec;
        fs::path parentDir = fsPath.parent_path();
        if (!parentDir.empty() && !fs::exists(parentDir, ec)) {
            fs::create_directories(parentDir, ec);
        }

        std::ofstream file(fsPath, std::ios::binary);
        if (!file.is_open()) {
            return ToolResult::fail("Cannot open file for writing: " + path);
        }

        file << content;
        file.close();

        if (file.fail()) {
            return ToolResult::fail("Failed to write file: " + path);
        }

        // Build result with content preview (JackProAi shows file content after write)
        std::string msg = "File written: " + path + " (" + std::to_string(content.size()) + " bytes)";
        std::string preview;
        std::istringstream iss(content);
        std::string line;
        int lineCount = 0;
        while (std::getline(iss, line) && lineCount < 20) {
            if (line.size() > 120) line = line.substr(0, 120) + "...";
            preview += "  " + line + "\n";
            lineCount++;
        }
        if (!preview.empty()) {
            msg += "\n" + preview;
        }

        FileStateCache::getInstance().invalidate(path);
        return ToolResult::ok(msg);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Write " + input.value("file_path", "file");
    }
};

} // namespace closecrab
