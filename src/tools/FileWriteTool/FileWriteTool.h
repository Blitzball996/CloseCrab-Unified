#pragma once

#include "../Tool.h"
#include "../../core/FileStateCache.h"
#include <fstream>
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

        // Ensure parent directory exists
        fs::path parentDir = fs::path(path).parent_path();
        if (!parentDir.empty() && !fs::exists(parentDir)) {
            fs::create_directories(parentDir);
        }

        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return ToolResult::fail("Cannot open file for writing: " + path);
        }

        file << content;
        file.close();

        if (file.fail()) {
            return ToolResult::fail("Failed to write file: " + path);
        }

        FileStateCache::getInstance().invalidate(path);
        return ToolResult::ok("File written: " + path + " (" + std::to_string(content.size()) + " bytes)");
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Write " + input.value("file_path", "file");
    }
};

} // namespace closecrab
