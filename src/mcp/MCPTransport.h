#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace closecrab {

// Transport abstraction for the MCP JSON-RPC channel.
//
// MCP defines several ways for a client and server to exchange JSON-RPC
// messages ("transports"):
//   - stdio:           the server runs as a local child process; messages go
//                      over its stdin/stdout (newline-delimited JSON).
//   - sse:             the server is remote; the client opens an SSE stream to
//                      receive messages and POSTs requests to an endpoint URL.
//   - streamable http: the newer remote transport; the client POSTs JSON-RPC
//                      and the server replies either with a single JSON body or
//                      an SSE stream on the same connection.
//
// Each concrete transport implements send()/receive() over its own channel.
// MCPClient drives the JSON-RPC protocol (initialize, tools/list, ...) on top.
class MCPTransport {
public:
    virtual ~MCPTransport() = default;

    // Establish the underlying channel (spawn process / open HTTP session).
    virtual bool start() = 0;

    // Tear down the channel.
    virtual void stop() = 0;

    // Send one JSON-RPC message (request or notification).
    virtual bool send(const nlohmann::json& message) = 0;

    // Block until the next JSON-RPC message arrives, or until timeout.
    // Returns false on timeout / closed channel. On success, `out` holds the
    // parsed message. Notifications and responses both come through here; the
    // caller (MCPClient) demultiplexes by id.
    virtual bool receive(nlohmann::json& out, int timeoutMs) = 0;

    virtual bool isOpen() const = 0;
};

// Factory: build the right transport for a server config.
//   transport == "sse"                       -> SSE (remote)
//   transport == "http"/"streamable-http"    -> Streamable HTTP (remote)
//   anything else (incl. "stdio")            -> local subprocess
std::unique_ptr<MCPTransport> makeTransport(
        const std::string& transport,
        const std::string& command,
        const std::vector<std::string>& args,
        const std::map<std::string, std::string>& env,
        const std::string& url,
        const std::map<std::string, std::string>& headers,
        const std::string& bearer);

} // namespace closecrab
