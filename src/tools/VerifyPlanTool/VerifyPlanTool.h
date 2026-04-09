#pragma once
#include "../Tool.h"
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace closecrab {
namespace fs = std::filesystem;

class VerifyPlanTool : public Tool {
public:
    std::string getName() const override { return "VerifyPlanExecution"; }
    std::string getDescription() const override {
        return "Verify that a plan was executed correctly by checking expected file changes.";
    }
    std::string getCategory() const override { return "workflow"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"checks",{{"type","array"},{"items",{{"type","object"},{"properties",{
                {"type",{{"type","string"},{"description","file_exists, file_contains, file_not_exists, command_succeeds"}}},
                {"path",{{"type","string"}}},
                {"content",{{"type","string"}}},
                {"command",{{"type","string"}}}
            }}}}}}
        }},{"required",{"checks"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto checks = input["checks"];
        int passed = 0, failed = 0;
        std::string report;

        for (const auto& check : checks) {
            std::string type = check.value("type", "");
            std::string path = check.value("path", "");
            bool ok = false;
            std::string detail;

            // Resolve relative paths
            fs::path fullPath = path.empty() ? fs::path() : fs::path(ctx.cwd) / path;

            if (type == "file_exists") {
                ok = fs::exists(fullPath);
                detail = ok ? "exists" : "NOT FOUND";
            } else if (type == "file_not_exists") {
                ok = !fs::exists(fullPath);
                detail = ok ? "correctly absent" : "UNEXPECTEDLY EXISTS";
            } else if (type == "file_contains") {
                std::string needle = check.value("content", "");
                if (fs::exists(fullPath)) {
                    std::ifstream f(fullPath);
                    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    ok = content.find(needle) != std::string::npos;
                    detail = ok ? "contains expected content" : "MISSING expected content";
                } else {
                    ok = false;
                    detail = "file not found";
                }
            } else if (type == "command_succeeds") {
                std::string cmd = check.value("command", "");
                if (!cmd.empty()) {
                    int ret = std::system(cmd.c_str());
                    ok = (ret == 0);
                    detail = ok ? "exit code 0" : "exit code " + std::to_string(ret);
                } else {
                    ok = false;
                    detail = "no command specified";
                }
            } else {
                detail = "unknown check type: " + type;
            }

            if (ok) passed++; else failed++;
            report += (ok ? "[PASS] " : "[FAIL] ") + type + " " + path + " — " + detail + "\n";
        }

        std::string summary = std::to_string(passed) + "/" + std::to_string(passed + failed) + " checks passed\n\n" + report;
        return failed == 0 ? ToolResult::ok(summary) : ToolResult::ok(summary);
    }
};

} // namespace closecrab
