#pragma once

#include "../Tool.h"
#include <cstdio>
#include <array>
#include <memory>
#include <sstream>
#include <filesystem>

namespace closecrab {

class GrepTool : public Tool {
public:
    std::string getName() const override { return "Grep"; }
    std::string getDescription() const override {
        return "Search file contents using regex. Supports context lines, file type filters, "
               "and multiple output modes (content, files_with_matches, count).";
    }
    std::string getCategory() const override { return "file"; }
    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"pattern", {{"type", "string"}, {"description", "Regex pattern to search for"}}},
                {"path", {{"type", "string"}, {"description", "File or directory to search"}}},
                {"glob", {{"type", "string"}, {"description", "Glob filter (e.g. \"*.cpp\")"}}},
                {"output_mode", {{"type", "string"}, {"description", "content, files_with_matches, or count"}}},
                {"-i", {{"type", "boolean"}, {"description", "Case insensitive"}}},
                {"-n", {{"type", "boolean"}, {"description", "Show line numbers"}}},
                {"-A", {{"type", "integer"}, {"description", "Lines after match"}}},
                {"-B", {{"type", "integer"}, {"description", "Lines before match"}}},
                {"-C", {{"type", "integer"}, {"description", "Context lines (before and after)"}}},
                {"head_limit", {{"type", "integer"}, {"description", "Max output lines (default 250)"}}},
                {"type", {{"type", "string"}, {"description", "File type filter (e.g. cpp, py, js)"}}}
            }},
            {"required", {"pattern"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
        std::string pattern = input["pattern"].get<std::string>();
        std::string searchPath = input.value("path", ctx.cwd);
        std::string outputMode = input.value("output_mode", "files_with_matches");
        int headLimit = input.value("head_limit", 250);

        // Build rg command
        std::string cmd = "rg";

        // Output mode
        if (outputMode == "files_with_matches") {
            cmd += " -l";
        } else if (outputMode == "count") {
            cmd += " -c";
        } else {
            // content mode
            if (input.value("-n", true)) cmd += " -n";
        }

        // Case insensitive
        if (input.value("-i", false)) cmd += " -i";

        // Context lines
        if (input.contains("-C")) cmd += " -C " + std::to_string(input["-C"].get<int>());
        else {
            if (input.contains("-A")) cmd += " -A " + std::to_string(input["-A"].get<int>());
            if (input.contains("-B")) cmd += " -B " + std::to_string(input["-B"].get<int>());
        }

        // Glob filter
        if (input.contains("glob")) {
            cmd += " --glob \"" + input["glob"].get<std::string>() + "\"";
        }

        // Type filter
        if (input.contains("type")) {
            cmd += " --type " + input["type"].get<std::string>();
        }

        // Pattern and path
        cmd += " -- \"" + pattern + "\" \"" + searchPath + "\"";

        // Head limit
        if (headLimit > 0) {
            cmd += " | head -n " + std::to_string(headLimit);
        }

        // Execute
        std::string output = execCommand(cmd);

        if (output.empty()) {
            return ToolResult::ok("No matches found for pattern: " + pattern);
        }

        // Count results
        int lineCount = 0;
        for (char c : output) if (c == '\n') lineCount++;

        nlohmann::json data = {
            {"mode", outputMode},
            {"numLines", lineCount},
            {"appliedLimit", headLimit}
        };

        return ToolResult::ok(output, data);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Grep " + input.value("pattern", "...");
    }

private:
    static std::string execCommand(const std::string& cmd) {
        std::string result;
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif
        if (!pipe) return "";

        std::array<char, 4096> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    }
};

} // namespace closecrab
