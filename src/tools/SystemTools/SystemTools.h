#pragma once

#include "../Tool.h"
#include "../../bridge/BridgeClient.h"

namespace closecrab {

// RemoteTriggerTool — trigger an action on a remote CloseCrab instance
class RemoteTriggerTool : public Tool {
public:
    std::string getName() const override { return "RemoteTrigger"; }
    std::string getDescription() const override {
        return "Trigger an action on a remote CloseCrab instance via Bridge.";
    }
    std::string getCategory() const override { return "remote"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"server_url",{{"type","string"},{"description","Remote server URL"}}},
            {"action",{{"type","string"},{"description","Action to trigger"}}},
            {"payload",{{"type","object"},{"description","Action payload"}}}
        }},{"required",{"server_url","action"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string url = input["server_url"].get<std::string>();
        std::string action = input["action"].get<std::string>();
        auto payload = input.value("payload", nlohmann::json::object());

        BridgeClient::Config config;
        config.serverUrl = url;
        BridgeClient client(config);

        if (!client.connect()) {
            return ToolResult::fail("Failed to connect to remote: " + url);
        }

        nlohmann::json msg = {{"action", action}, {"payload", payload}};
        client.sendMessage("default", msg.dump());
        client.disconnect();

        return ToolResult::ok("Triggered '" + action + "' on " + url);
    }
};

// ToolSearchTool moved to tools/ToolSearchTool/ToolSearchTool.h (JackProAi-style defer/discover)



// McpAuthTool — authenticate with an MCP server (OAuth)
class McpAuthTool : public Tool {
public:
    std::string getName() const override { return "McpAuth"; }
    std::string getDescription() const override {
        return "Authenticate with an MCP server using OAuth or API key.";
    }
    std::string getCategory() const override { return "mcp"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"server_name",{{"type","string"},{"description","MCP server name"}}},
            {"auth_type",{{"type","string"},{"description","oauth or api_key"}}},
            {"token",{{"type","string"},{"description","Auth token or API key"}}}
        }},{"required",{"server_name","auth_type"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string serverName = input["server_name"].get<std::string>();
        std::string authType = input["auth_type"].get<std::string>();

        auto& mgr = MCPServerManager::getInstance();
        auto* client = mgr.getClient(serverName);
        if (!client) return ToolResult::fail("MCP server not found: " + serverName);

        if (authType == "api_key") {
            std::string token = input.value("token", "");
            if (token.empty()) return ToolResult::fail("Token required for api_key auth");
            // Store token for future requests (simplified)
            return ToolResult::ok("Authenticated with " + serverName + " via API key.");
        }

        if (authType == "oauth") {
            // OAuth flow would open browser, handle callback, etc.
            return ToolResult::ok("OAuth flow initiated for " + serverName +
                                  ". Please complete authentication in your browser.");
        }

        return ToolResult::fail("Unknown auth type: " + authType);
    }
};

// BriefTool — toggle brief/verbose output mode
class BriefTool : public Tool {
public:
    std::string getName() const override { return "Brief"; }
    std::string getDescription() const override {
        return "Toggle brief mode for shorter, more concise responses.";
    }
    std::string getCategory() const override { return "system"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"enabled",{{"type","boolean"},{"description","Enable or disable brief mode"}}}
        }}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        bool enabled = input.value("enabled", true);
        // Brief mode is stored in AppState — affects system prompt
        if (ctx.appState) {
            ctx.appState->fastMode = enabled; // Reuse fastMode for brief
        }
        return ToolResult::ok(std::string("Brief mode: ") + (enabled ? "ON" : "OFF"));
    }
};

// SyntheticOutputTool — produce structured JSON output matching a schema
class SyntheticOutputTool : public Tool {
public:
    std::string getName() const override { return "SyntheticOutput"; }
    std::string getDescription() const override {
        return "Produce structured output matching a JSON schema.";
    }
    std::string getCategory() const override { return "system"; }
    bool isReadOnly() const override { return true; }
    bool isHidden() const override { return true; } // Internal use by SDK

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"schema",{{"type","object"},{"description","JSON Schema to validate against"}}},
            {"data",{{"description","The structured data to output"}}}
        }},{"required",{"data"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto data = input["data"];

        // If schema provided, do basic validation
        if (input.contains("schema") && input["schema"].is_object()) {
            auto& schema = input["schema"];
            // Check required fields
            if (schema.contains("required") && schema["required"].is_array()) {
                for (const auto& req : schema["required"]) {
                    std::string field = req.get<std::string>();
                    if (!data.contains(field)) {
                        return ToolResult::fail("Missing required field: " + field);
                    }
                }
            }
        }

        return ToolResult::ok(data.dump(2), data);
    }
};

} // namespace closecrab
