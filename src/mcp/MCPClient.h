#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>
#include "MCPTransport.h"
#include <thread>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <unordered_map>

namespace closecrab {

// JSON-RPC 2.0 message types for MCP protocol
struct MCPRequest {
    std::string method;
    nlohmann::json params;
    int id = 0;

    nlohmann::json toJson() const {
        return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}, {"id", id}};
    }
};

struct MCPResponse {
    int id = 0;
    nlohmann::json result;
    nlohmann::json error;  // {code, message, data}
    bool isError() const { return !error.is_null(); }
};

// MCP tool definition received from server
struct MCPToolDef {
    std::string name;
    std::string description;
    nlohmann::json inputSchema;
};

// MCP resource
struct MCPResource {
    std::string uri;
    std::string name;
    std::string mimeType;
    std::string description;
};

// MCP server configuration
struct MCPServerConfig {
    std::string name;
    std::string command;                // e.g. "npx"
    std::vector<std::string> args;      // e.g. ["-y", "@modelcontextprotocol/server-filesystem"]
    std::map<std::string, std::string> env;
    // Transport: "stdio" (local subprocess), "sse" (remote Server-Sent Events),
    // or "http" (remote Streamable HTTP). Defaults to stdio.
    std::string transport = "stdio";
    std::string url;                    // for sse/http transport (server endpoint)
    std::map<std::string, std::string> headers;  // extra HTTP headers (sse/http)
    // OAuth: when set, the client performs an OAuth flow and sends a bearer token.
    bool oauth = false;
    std::string oauthClientId;
    std::string oauthClientSecret;
    std::string oauthAuthUrl;
    std::string oauthTokenUrl;
    std::string oauthScope;
};

// MCP server "capabilities" advertised during initialize. Tracks what the
// connected server supports so the client can decide whether to call
// resources/subscribe, expect list_changed notifications, etc.
struct MCPServerCapabilities {
    bool tools = false;
    bool toolsListChanged = false;
    bool resources = false;
    bool resourcesSubscribe = false;
    bool resourcesListChanged = false;
    bool prompts = false;
    bool promptsListChanged = false;
    bool logging = false;
    bool completions = false;
};

// Handlers the client installs so the server can call back into us
// (MCP is bidirectional). All optional; if unset the client replies with a
// "method not found"/"not supported" error to the server.
struct MCPClientHandlers {
    // sampling/createMessage: server asks our LLM to generate a completion.
    // Input: the raw JSON-RPC params. Output: the JSON-RPC result.
    std::function<nlohmann::json(const nlohmann::json& params)> onSampling;
    // elicitation/create: server asks the user for structured input.
    std::function<nlohmann::json(const nlohmann::json& params)> onElicitation;
    // roots/list: server asks for the client's workspace roots.
    std::function<nlohmann::json()> onListRoots;
    // notifications/* from the server (e.g. tools/list_changed). Receives the
    // method name and params. Used to invalidate caches / refresh tools.
    std::function<void(const std::string& method, const nlohmann::json& params)> onNotification;
};

// MCP client — communicates with one MCP server via stdio
class MCPClient {
public:
    explicit MCPClient(const MCPServerConfig& config);
    ~MCPClient();

    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }
    // Reconnect if the transport dropped (no-op if already connected).
    void ensureConnected() { if (!connected_) attemptReconnect(); }

    // MCP protocol methods
    std::vector<MCPToolDef> listTools();
    nlohmann::json callTool(const std::string& name, const nlohmann::json& args);
    std::vector<MCPResource> listResources();
    std::string readResource(const std::string& uri);

    // resources/subscribe + resources/unsubscribe (only if the server
    // advertised resources.subscribe). Server then sends
    // notifications/resources/updated for that uri.
    bool subscribeResource(const std::string& uri);
    bool unsubscribeResource(const std::string& uri);

    // MCP prompts (prompts/list, prompts/get). Returns the prompt list as raw
    // JSON entries ({name, description, arguments}); getPrompt renders a prompt
    // with the given arguments and returns the assembled message text.
    std::vector<nlohmann::json> listPrompts();
    std::string getPrompt(const std::string& name, const nlohmann::json& args);

    const MCPServerConfig& getConfig() const { return config_; }
    const std::string& getName() const { return config_.name; }
    const MCPServerCapabilities& getCapabilities() const { return caps_; }

    // Install client-side handlers (sampling/elicitation/roots/notifications).
    void setHandlers(const MCPClientHandlers& h) { handlers_ = h; }

private:
    MCPResponse sendRequest(const MCPRequest& req);
    void sendNotification(const std::string& method, const nlohmann::json& params);
    void writeLine(const std::string& line);

    // Background reader: pulls every inbound message and demultiplexes it into
    // responses (matched by id), server->client requests, and notifications.
    void readLoop();
    void handleServerRequest(const nlohmann::json& msg);
    void handleNotification(const nlohmann::json& msg);
    void parseServerCapabilities(const nlohmann::json& caps);
    void attemptReconnect();

    MCPServerConfig config_;
    std::atomic<bool> connected_{false};
    std::atomic<int> nextId_{1};
    MCPServerCapabilities caps_;
    MCPClientHandlers handlers_;
    std::string bearer_;  // resolved OAuth token for remote transports

    // All I/O goes through the transport (stdio / sse / streamable-http).
    std::unique_ptr<MCPTransport> transport_;

    // Reader thread + response demux.
    std::thread reader_;
    std::atomic<bool> readerRunning_{false};
    std::mutex respMutex_;
    std::condition_variable respCv_;
    std::unordered_map<int, nlohmann::json> responses_;  // id -> full message
};

// MCP Server Manager — manages multiple MCP server connections
class MCPServerManager {
public:
    static MCPServerManager& getInstance();

    bool addServer(const MCPServerConfig& config);
    void removeServer(const std::string& name);
    MCPClient* getClient(const std::string& name);
    std::vector<std::string> getServerNames() const;

    // Aggregate tools from all connected servers
    std::vector<std::pair<std::string, MCPToolDef>> getAllTools();

    // Install the handlers (sampling/elicitation/roots/notifications) applied
    // to every server connection. Call before loadFromSettings/addServer so
    // new clients pick them up; also re-applies to already-connected clients.
    void setHandlers(const MCPClientHandlers& h);

    // Tool cache invalidation: a list_changed notification clears the cache so
    // getAllTools() re-queries on next call.
    void invalidateToolCache();

    // Load server configs from settings.json
    void loadFromSettings(const nlohmann::json& mcpServers);

    void disconnectAll();

private:
    MCPServerManager() = default;
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<MCPClient>> clients_;
    MCPClientHandlers handlers_;

    // Cache of aggregated tools (server, def). Rebuilt lazily; cleared by
    // invalidateToolCache() when a server signals tools/list_changed.
    std::vector<std::pair<std::string, MCPToolDef>> toolCache_;
    std::atomic<bool> toolCacheValid_{false};
};

} // namespace closecrab
