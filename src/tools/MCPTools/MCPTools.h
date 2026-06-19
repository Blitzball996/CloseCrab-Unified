#pragma once

#include "../Tool.h"
#include "../../mcp/MCPClient.h"

namespace closecrab {

// MCPTool — call a tool on an MCP server
class MCPTool : public Tool {
public:
    std::string getName() const override { return "MCPTool"; }
    std::string getDescription() const override {
        return "Call a tool provided by an MCP server.";
    }
    std::string getCategory() const override { return "mcp"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"server_name",{{"type","string"},{"description","MCP server name"}}},
            {"tool_name",{{"type","string"},{"description","Tool name on the server"}}},
            {"arguments",{{"type","object"},{"description","Tool arguments"}}}
        }},{"required",{"server_name","tool_name"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto& mgr = MCPServerManager::getInstance();
        std::string serverName = input["server_name"].get<std::string>();
        std::string toolName = input["tool_name"].get<std::string>();
        auto args = input.value("arguments", nlohmann::json::object());

        auto* client = mgr.getClient(serverName);
        if (!client) return ToolResult::fail("MCP server not found: " + serverName);
        if (!client->isConnected()) return ToolResult::fail("MCP server not connected: " + serverName);

        auto result = client->callTool(toolName, args);
        if (result.contains("error")) {
            return ToolResult::fail("MCP tool error: " + result["error"].dump());
        }

        // Extract text content
        if (result.contains("content") && result["content"].is_array()) {
            std::string text;
            for (const auto& c : result["content"]) {
                if (c.value("type", "") == "text") text += c.value("text", "") + "\n";
            }
            return ToolResult::ok(text, result);
        }
        return ToolResult::ok(result.dump(2), result);
    }
};

// ListMcpResourcesTool
class ListMcpResourcesTool : public Tool {
public:
    std::string getName() const override { return "ListMcpResources"; }
    std::string getDescription() const override { return "List resources from an MCP server."; }
    std::string getCategory() const override { return "mcp"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"server_name",{{"type","string"},{"description","MCP server name"}}}
        }},{"required",{"server_name"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto* client = MCPServerManager::getInstance().getClient(input["server_name"].get<std::string>());
        if (!client || !client->isConnected()) return ToolResult::fail("MCP server not available");

        auto resources = client->listResources();
        std::string out;
        for (const auto& r : resources) {
            out += r.uri + " (" + r.name + ") - " + r.description + "\n";
        }
        if (out.empty()) out = "No resources available.\n";
        return ToolResult::ok(out);
    }
};

// ReadMcpResourceTool
class ReadMcpResourceTool : public Tool {
public:
    std::string getName() const override { return "ReadMcpResource"; }
    std::string getDescription() const override { return "Read a resource from an MCP server."; }
    std::string getCategory() const override { return "mcp"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"server_name",{{"type","string"}}},
            {"uri",{{"type","string"},{"description","Resource URI"}}}
        }},{"required",{"server_name","uri"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto* client = MCPServerManager::getInstance().getClient(input["server_name"].get<std::string>());
        if (!client || !client->isConnected()) return ToolResult::fail("MCP server not available");
        return ToolResult::ok(client->readResource(input["uri"].get<std::string>()));
    }
};

// MCPProxyTool — exposes ONE tool from an MCP server as a first-class CloseCrab
// tool, so the model sees the server's tools individually (with their real name,
// description and input schema) instead of one opaque "MCPTool" proxy. The model
// can then call e.g. mcp__codebase-memory__search_graph directly, the same way
// it calls a built-in tool. Built at startup by iterating
// MCPServerManager::getAllTools() (see main.cpp), one proxy per (server, tool).
//
// Naming: "mcp__<server>__<tool>" (Claude Code convention) — namespaced so MCP
// tools never collide with the 59 built-in tools.
class MCPProxyTool : public Tool {
public:
    MCPProxyTool(std::string serverName, MCPToolDef def)
        : serverName_(std::move(serverName)), def_(std::move(def)) {
        name_ = "mcp__" + serverName_ + "__" + def_.name;
    }

    std::string getName() const override { return name_; }
    std::string getDescription() const override {
        std::string d = def_.description.empty()
            ? ("Tool '" + def_.name + "' from MCP server '" + serverName_ + "'.")
            : def_.description;
        return "[MCP:" + serverName_ + "] " + d;
    }
    std::string getCategory() const override { return "mcp"; }

    nlohmann::json getInputSchema() const override {
        // Hand the server's own JSON Schema straight through, so the model sees
        // the exact required args. Fall back to a permissive object schema if the
        // server didn't advertise one.
        if (def_.inputSchema.is_object() && !def_.inputSchema.empty())
            return def_.inputSchema;
        return {{"type", "object"}, {"properties", nlohmann::json::object()}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        (void)ctx;
        auto* client = MCPServerManager::getInstance().getClient(serverName_);
        if (!client) return ToolResult::fail("MCP server not found: " + serverName_);
        if (!client->isConnected()) return ToolResult::fail("MCP server not connected: " + serverName_);

        auto result = client->callTool(def_.name, input);
        if (result.contains("error")) {
            return ToolResult::fail("MCP tool error: " + result["error"].dump());
        }
        // Unwrap MCP content blocks into plain text (same as MCPTool).
        if (result.contains("content") && result["content"].is_array()) {
            std::string text;
            for (const auto& c : result["content"]) {
                if (c.value("type", "") == "text") text += c.value("text", "") + "\n";
            }
            return ToolResult::ok(text, result);
        }
        return ToolResult::ok(result.dump(2), result);
    }

    // MCP tools self-describe read-only-ness via annotations when present; we
    // can't know in general, so treat them as non-read-only (asks permission)
    // unless the server marked the tool readOnlyHint.
    bool isReadOnly() const override {
        if (def_.inputSchema.is_object()) {
            auto ann = def_.inputSchema.value("annotations", nlohmann::json::object());
            if (ann.is_object() && ann.value("readOnlyHint", false)) return true;
        }
        return false;
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return name_ + "(" + (input.is_null() ? "" : input.dump()) + ")";
    }

private:
    std::string serverName_;
    MCPToolDef def_;
    std::string name_;
};

} // namespace closecrab
