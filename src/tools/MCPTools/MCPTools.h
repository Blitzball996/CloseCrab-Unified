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

} // namespace closecrab
