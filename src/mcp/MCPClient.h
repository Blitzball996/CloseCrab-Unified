#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>

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
    std::string transport = "stdio";    // "stdio" or "sse"
    std::string url;                    // for SSE transport
};

// MCP client — communicates with one MCP server via stdio
class MCPClient {
public:
    explicit MCPClient(const MCPServerConfig& config);
    ~MCPClient();

    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    // MCP protocol methods
    std::vector<MCPToolDef> listTools();
    nlohmann::json callTool(const std::string& name, const nlohmann::json& args);
    std::vector<MCPResource> listResources();
    std::string readResource(const std::string& uri);

    const MCPServerConfig& getConfig() const { return config_; }
    const std::string& getName() const { return config_.name; }

private:
    MCPResponse sendRequest(const MCPRequest& req);
    std::string readLine();
    void writeLine(const std::string& line);

    MCPServerConfig config_;
    bool connected_ = false;
    int nextId_ = 1;

#ifdef _WIN32
    void* processHandle_ = nullptr;
    void* stdinWrite_ = nullptr;
    void* stdoutRead_ = nullptr;
#else
    int pid_ = -1;
    int stdinFd_ = -1;
    int stdoutFd_ = -1;
#endif
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

    // Load server configs from settings.json
    void loadFromSettings(const nlohmann::json& mcpServers);

    void disconnectAll();

private:
    MCPServerManager() = default;
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<MCPClient>> clients_;
};

} // namespace closecrab
