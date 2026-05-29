#pragma once

#include "../Tool.h"
#include "../../services/AgentProgress.h"
#include "../../agents/AgentManager.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>

namespace closecrab {

// CtxInspectTool — inspect current context (message count, token estimate, tools)
class CtxInspectTool : public Tool {
public:
    std::string getName() const override { return "CtxInspect"; }
    std::string getDescription() const override { return "Inspect current conversation context: message count, estimated tokens, active tools."; }
    std::string getCategory() const override { return "debug"; }
    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json&) override {
        int msgCount = ctx.messages ? (int)ctx.messages->size() : 0;
        int estTokens = 0;
        for (const auto& m : *ctx.messages) {
            auto j = m.toApiJson();
            estTokens += (int)j.dump().size() / 4;
        }
        std::string result = "Context:\n";
        result += "  Messages: " + std::to_string(msgCount) + "\n";
        result += "  Estimated tokens: ~" + std::to_string(estTokens / 1000) + "K\n";
        result += "  CWD: " + ctx.cwd + "\n";
        if (ctx.toolRegistry) {
            int toolCount = 0;
            for (auto* t : ctx.toolRegistry->getAllTools()) if (t && t->isEnabled()) toolCount++;
            result += "  Active tools: " + std::to_string(toolCount) + "\n";
        }
        return ToolResult::ok(result);
    }
};

// ListPeersTool — list active agents/peers
class ListPeersTool : public Tool {
public:
    std::string getName() const override { return "ListPeers"; }
    std::string getDescription() const override { return "List all active sub-agents and their status."; }
    std::string getCategory() const override { return "agent"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {}}};
    }

    ToolResult call(ToolContext&, const nlohmann::json&) override {
        auto agents = AgentManager::getInstance().listAgents();
        if (agents.empty()) return ToolResult::ok("No active agents.");
        std::string result;
        for (const auto& [id, status] : agents) {
            std::string statusStr;
            switch (status) {
                case AgentStatus::RUNNING: statusStr = "running"; break;
                case AgentStatus::COMPLETED: statusStr = "completed"; break;
                case AgentStatus::FAILED: statusStr = "failed"; break;
                case AgentStatus::KILLED: statusStr = "killed"; break;
                default: statusStr = "pending"; break;
            }
            result += id + ": " + statusStr + "\n";
        }
        return ToolResult::ok(result);
    }
};

// LocalMemoryRecallTool
class LocalMemoryRecallTool : public Tool {
public:
    std::string getName() const override { return "LocalMemoryRecall"; }
    std::string getDescription() const override { return "Recall memories by keyword search."; }
    std::string getCategory() const override { return "memory"; }
    bool isReadOnly() const override { return true; }
    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {{"query", {{"type", "string"}, {"description", "Search query"}}}}}, {"required", {"query"}}};
    }
    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
        std::string query = input["query"].get<std::string>();
        fs::path memDir = fs::path(ctx.cwd) / ".crab" / "memory";
        if (!fs::exists(memDir)) return ToolResult::ok("No memories found.");
        std::string results; int count = 0;
        for (auto& e : fs::directory_iterator(memDir)) {
            if (!e.is_regular_file()) continue;
            std::ifstream f(e.path()); std::string c((std::istreambuf_iterator<char>(f)), {});
            if (c.find(query) != std::string::npos) { results += "--- " + e.path().filename().string() + " ---\n" + c.substr(0, 500) + "\n\n"; if (++count >= 5) break; }
        }
        return ToolResult::ok(results.empty() ? "No memories matching: " + query : results);
    }
};

// PushNotificationTool
class PushNotificationTool : public Tool {
public:
    std::string getName() const override { return "PushNotification"; }
    std::string getDescription() const override { return "Send a system notification."; }
    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {{"title", {{"type", "string"}}}, {"body", {{"type", "string"}}}}}, {"required", {"title", "body"}}};
    }
    ToolResult call(ToolContext&, const nlohmann::json& input) override {
        std::string t = input["title"].get<std::string>(), b = input["body"].get<std::string>();
        std::system(("powershell -Command \"[void][System.Reflection.Assembly]::LoadWithPartialName('System.Windows.Forms');$n=New-Object System.Windows.Forms.NotifyIcon;$n.Icon=[System.Drawing.SystemIcons]::Information;$n.Visible=$true;$n.ShowBalloonTip(5000,'" + t + "','" + b + "','Info');Start-Sleep 6;$n.Dispose()\"").c_str());
        return ToolResult::ok("Notification sent: " + t);
    }
};

// ReviewArtifactTool
class ReviewArtifactTool : public Tool {
public:
    std::string getName() const override { return "ReviewArtifact"; }
    std::string getDescription() const override { return "Get git diff for code review."; }
    bool isReadOnly() const override { return true; }
    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {{"target", {{"type", "string"}, {"description", "File or ref"}}}}}, {"required", {"target"}}};
    }
    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string target = input["target"].get<std::string>();
        FILE* pipe = popen(("cd \"" + ctx.cwd + "\" && git diff " + target + " 2>&1").c_str(), "r");
        if (!pipe) return ToolResult::fail("git diff failed");
        std::string out; char buf[4096]; while (fgets(buf, sizeof(buf), pipe)) out += buf; pclose(pipe);
        if (out.size() > 8000) out = out.substr(0, 8000) + "\n[truncated]";
        return ToolResult::ok(out.empty() ? "No changes" : out);
    }
};

// SendUserFileTool
class SendUserFileTool : public Tool {
public:
    std::string getName() const override { return "SendUserFile"; }
    std::string getDescription() const override { return "Copy file to user's Desktop."; }
    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {{"file_path", {{"type", "string"}}}}}, {"required", {"file_path"}}};
    }
    ToolResult call(ToolContext&, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
        std::string p = input["file_path"].get<std::string>();
        if (!fs::exists(p)) return ToolResult::fail("Not found: " + p);
        const char* prof = std::getenv("USERPROFILE");
        if (prof) { fs::path d = fs::path(prof) / "Desktop" / fs::path(p).filename(); std::error_code ec; fs::copy_file(p, d, fs::copy_options::overwrite_existing, ec); if (!ec) return ToolResult::ok("Copied to: " + d.string()); }
        return ToolResult::ok("File at: " + p);
    }
};

// SubscribePRTool
class SubscribePRTool : public Tool {
public:
    std::string getName() const override { return "SubscribePR"; }
    std::string getDescription() const override { return "Check GitHub PR status via gh CLI."; }
    bool isReadOnly() const override { return true; }
    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {{"pr_number", {{"type", "integer"}}}, {"repo", {{"type", "string"}}}}}, {"required", {"pr_number"}}};
    }
    ToolResult call(ToolContext&, const nlohmann::json& input) override {
        int pr = input["pr_number"].get<int>(); std::string repo = input.value("repo", "");
        std::string cmd = "gh pr view " + std::to_string(pr) + " --json state,reviews,statusCheckRollup";
        if (!repo.empty()) cmd += " -R " + repo;
        FILE* pipe = popen(cmd.c_str(), "r"); if (!pipe) return ToolResult::fail("gh not available");
        std::string out; char buf[4096]; while (fgets(buf, sizeof(buf), pipe)) out += buf; pclose(pipe);
        return ToolResult::ok(out.empty() ? "No data" : out.substr(0, 6000));
    }
};

// VaultHttpFetchTool
class VaultHttpFetchTool : public Tool {
public:
    std::string getName() const override { return "VaultHttpFetch"; }
    std::string getDescription() const override { return "Fetch URL with Bearer token from env var."; }
    bool isReadOnly() const override { return true; }
    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {{"url", {{"type", "string"}}}, {"auth_env", {{"type", "string"}}}}}, {"required", {"url"}}};
    }
    ToolResult call(ToolContext&, const nlohmann::json& input) override {
        std::string url = input["url"].get<std::string>(), env = input.value("auth_env", "VAULT_TOKEN");
        const char* tok = std::getenv(env.c_str());
        std::string auth = tok ? std::string(" -H \"Authorization: Bearer ") + tok + "\"" : "";
        FILE* pipe = popen(("curl -s" + auth + " \"" + url + "\"").c_str(), "r");
        if (!pipe) return ToolResult::fail("curl not available");
        std::string out; char buf[4096]; while (fgets(buf, sizeof(buf), pipe)) out += buf; pclose(pipe);
        if (out.size() > 8000) out = out.substr(0, 8000) + "\n[truncated]";
        return ToolResult::ok(out);
    }
};

} // namespace closecrab