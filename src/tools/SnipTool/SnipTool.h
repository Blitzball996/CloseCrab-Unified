#pragma once
#include "../Tool.h"
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace closecrab {
namespace fs = std::filesystem;

class SnipTool : public Tool {
public:
    std::string getName() const override { return "Snip"; }
    std::string getDescription() const override {
        return "Save code snippets or trim conversation history to free context.";
    }
    std::string getCategory() const override { return "workflow"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"action",{{"type","string"},{"description","save, trim, or list"}}},
            {"content",{{"type","string"},{"description","Content to save (for save action)"}}},
            {"name",{{"type","string"},{"description","Snippet name (for save action)"}}},
            {"count",{{"type","integer"},{"description","Number of old turns to remove (for trim)"}}}
        }},{"required",{"action"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string action = input["action"].get<std::string>();
        fs::path snippetDir = fs::path(ctx.cwd) / ".claude" / "snippets";

        if (action == "save") {
            std::string content = input.value("content", "");
            std::string name = input.value("name", "snippet");
            if (content.empty()) return ToolResult::fail("No content to save");
            if (!fs::exists(snippetDir)) fs::create_directories(snippetDir);
            fs::path filePath = snippetDir / (name + ".txt");
            std::ofstream f(filePath);
            if (!f) return ToolResult::fail("Cannot write to " + filePath.string());
            f << content;
            f.close();
            return ToolResult::ok("Saved snippet '" + name + "' (" + std::to_string(content.size()) + " chars)");

        } else if (action == "trim") {
            int count = input.value("count", 2);
            if (!ctx.messages || ctx.messages->empty()) return ToolResult::fail("No messages to trim");
            int removed = 0;
            // Remove oldest message pairs (skip system messages at the start)
            while (removed < count * 2 && ctx.messages->size() > 4) {
                // Find first non-system message
                size_t idx = 0;
                while (idx < ctx.messages->size() && (*ctx.messages)[idx].type == MessageType::SYSTEM) idx++;
                if (idx >= ctx.messages->size() - 2) break;
                ctx.messages->erase(ctx.messages->begin() + idx);
                removed++;
            }
            return ToolResult::ok("Trimmed " + std::to_string(removed) + " messages from history");

        } else if (action == "list") {
            if (!fs::exists(snippetDir)) return ToolResult::ok("No snippets saved yet.");
            std::string result;
            for (const auto& entry : fs::directory_iterator(snippetDir)) {
                if (entry.is_regular_file()) {
                    result += entry.path().filename().string() + " ("
                           + std::to_string(entry.file_size()) + " bytes)\n";
                }
            }
            return ToolResult::ok(result.empty() ? "No snippets found." : result);
        }
        return ToolResult::fail("Unknown action: " + action + ". Use save, trim, or list.");
    }
};

} // namespace closecrab
