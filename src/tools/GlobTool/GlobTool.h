#pragma once

#include "../Tool.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <regex>

namespace closecrab {

class GlobTool : public Tool {
public:
    std::string getName() const override { return "Glob"; }
    std::string getDescription() const override {
        return "Find files matching a glob pattern (e.g. \"**/*.cpp\", \"src/**/*.h\").";
    }
    std::string getCategory() const override { return "file"; }
    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"pattern", {{"type", "string"}, {"description", "Glob pattern to match"}}},
                {"path", {{"type", "string"}, {"description", "Directory to search in (default: cwd)"}}}
            }},
            {"required", {"pattern"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
        std::string pattern = input["pattern"].get<std::string>();
        std::string searchPath = input.value("path", ctx.cwd);

        if (!fs::exists(searchPath) || !fs::is_directory(searchPath)) {
            return ToolResult::fail("Directory not found: " + searchPath);
        }

        // Convert glob to regex
        std::string regexStr = globToRegex(pattern);
        std::regex re;
        try {
            re = std::regex(regexStr, std::regex::icase);
        } catch (const std::regex_error& e) {
            return ToolResult::fail("Invalid glob pattern: " + std::string(e.what()));
        }

        std::vector<std::string> matches;
        const int maxResults = 100;

        try {
            for (auto& entry : fs::recursive_directory_iterator(searchPath,
                    fs::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) continue;

                std::string relPath = fs::relative(entry.path(), searchPath).string();
                // Normalize separators
                std::replace(relPath.begin(), relPath.end(), '\\', '/');

                if (std::regex_match(relPath, re)) {
                    matches.push_back(relPath);
                    if ((int)matches.size() >= maxResults) break;
                }
            }
        } catch (const fs::filesystem_error&) {
            // Skip permission errors
        }

        // Sort by modification time (newest first)
        std::sort(matches.begin(), matches.end());

        bool truncated = (int)matches.size() >= maxResults;
        std::string result;
        for (const auto& m : matches) result += m + "\n";

        nlohmann::json data = {
            {"numFiles", matches.size()},
            {"filenames", matches},
            {"truncated", truncated}
        };

        if (matches.empty()) {
            return ToolResult::ok("No files matched pattern: " + pattern, data);
        }
        return ToolResult::ok(result, data);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Glob " + input.value("pattern", "*");
    }

private:
    static std::string globToRegex(const std::string& glob) {
        std::string regex;
        size_t i = 0;
        bool inBracket = false;
        while (i < glob.size()) {
            char c = glob[i];
            if (c == '*') {
                if (i + 1 < glob.size() && glob[i + 1] == '*') {
                    // ** matches any path
                    regex += ".*";
                    i += 2;
                    if (i < glob.size() && glob[i] == '/') i++; // skip trailing /
                    continue;
                }
                regex += "[^/]*"; // * matches within directory
            } else if (c == '?') {
                regex += "[^/]";
            } else if (c == '[') {
                inBracket = true;
                regex += c;
            } else if (c == ']') {
                inBracket = false;
                regex += c;
            } else if (c == '.') {
                regex += "\\.";
            } else if (c == '{') {
                regex += "(";
            } else if (c == '}') {
                regex += ")";
            } else if (c == ',' && !inBracket) {
                regex += "|";
            } else if (c == '+' || c == '(' || c == ')' || c == '^' || c == '$' || c == '|' || c == '\\') {
                // Escape regex special characters that aren't glob special
                regex += '\\';
                regex += c;
            } else {
                regex += c;
            }
            i++;
        }
        return regex;
    }
};

} // namespace closecrab
