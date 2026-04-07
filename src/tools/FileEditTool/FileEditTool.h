#pragma once

#include "../Tool.h"
#include "../../core/FileStateCache.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace closecrab {

class FileEditTool : public Tool {
public:
    std::string getName() const override { return "Edit"; }
    std::string getDescription() const override {
        return "Edit a file by replacing an exact string match with new content. "
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
                {"replace_all", {{"type", "boolean"}, {"description", "Replace all occurrences (default false)"}}}
            }},
            {"required", {"file_path", "old_string", "new_string"}}
        };
    }

    ValidationResult validateInput(const nlohmann::json& input) const override {
        auto base = Tool::validateInput(input);
        if (!base.valid) return base;

        if (input["old_string"].get<std::string>() == input["new_string"].get<std::string>()) {
            return ValidationResult::fail("old_string and new_string are identical");
        }
        return ValidationResult::ok();
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
        std::string path = input["file_path"].get<std::string>();
        std::string oldStr = input["old_string"].get<std::string>();
        std::string newStr = input["new_string"].get<std::string>();
        bool replaceAll = input.value("replace_all", false);

        if (!fs::exists(path)) {
            return ToolResult::fail("File not found: " + path);
        }

        // Read file
        std::ifstream inFile(path, std::ios::binary);
        if (!inFile.is_open()) {
            return ToolResult::fail("Cannot open file: " + path);
        }
        std::string content((std::istreambuf_iterator<char>(inFile)), {});
        inFile.close();

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
            return ToolResult::fail(
                "old_string found " + std::to_string(count) + " times. "
                "Provide more context to make it unique, or set replace_all=true.");
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
        std::ofstream outFile(path, std::ios::binary);
        if (!outFile.is_open()) {
            return ToolResult::fail("Cannot write to file: " + path);
        }
        outFile << modified;
        outFile.close();

        std::string msg = replaceAll
            ? "Replaced " + std::to_string(count) + " occurrences in " + path
            : "Replaced 1 occurrence in " + path;

        FileStateCache::getInstance().invalidate(path);
        return ToolResult::ok(msg);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Edit " + input.value("file_path", "file");
    }
};

} // namespace closecrab
