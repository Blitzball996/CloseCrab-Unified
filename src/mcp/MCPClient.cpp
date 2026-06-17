#include "MCPClient.h"
#include "MCPOAuth.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <chrono>

namespace closecrab {

MCPClient::MCPClient(const MCPServerConfig& config) : config_(config) {}

MCPClient::~MCPClient() { disconnect(); }

bool MCPClient::connect() {
    // Resolve a bearer token for remote transports when OAuth is configured.
    bearer_.clear();
    if (config_.oauth && (config_.transport == "sse" ||
                          config_.transport == "http" ||
                          config_.transport == "streamable-http")) {
        auto token = MCPOAuth::loadToken(config_.name);
        OAuthConfig oc;
        oc.clientId     = config_.oauthClientId;
        oc.clientSecret = config_.oauthClientSecret;
        oc.authUrl      = config_.oauthAuthUrl;
        oc.tokenUrl     = config_.oauthTokenUrl;
        oc.scope        = config_.oauthScope;
        // Refresh an expired token if we can, otherwise run the full flow.
        if (MCPOAuth::isExpired(token)) {
            if (!token.refreshToken.empty()) {
                auto refreshed = MCPOAuth::refresh(oc, token.refreshToken);
                if (!refreshed.accessToken.empty()) token = refreshed;
            }
            if (MCPOAuth::isExpired(token)) {
                token = MCPOAuth::authorize(oc);
            }
            if (!token.accessToken.empty()) MCPOAuth::saveToken(config_.name, token);
        }
        bearer_ = token.accessToken;
    }

    // Build the transport for this server's configured channel.
    transport_ = makeTransport(config_.transport, config_.command, config_.args,
                               config_.env, config_.url, config_.headers, bearer_);
    if (!transport_ || !transport_->start()) {
        spdlog::error("MCP: failed to start transport for '{}' (transport={})",
                      config_.name, config_.transport);
        transport_.reset();
        return false;
    }
    connected_ = true;

    // Start the background reader before the handshake so the initialize
    // response (and any server requests during init) are demultiplexed.
    readerRunning_ = true;
    reader_ = std::thread([this]() { readLoop(); });

    // Handshake: advertise our client capabilities (sampling/roots/elicitation
    // depending on which handlers are installed) so the server knows it can
    // call back into us.
    nlohmann::json clientCaps = nlohmann::json::object();
    if (handlers_.onSampling)    clientCaps["sampling"] = nlohmann::json::object();
    if (handlers_.onElicitation) clientCaps["elicitation"] = nlohmann::json::object();
    if (handlers_.onListRoots)   clientCaps["roots"] = {{"listChanged", true}};

    MCPRequest init;
    init.method = "initialize";
    init.params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", clientCaps},
        {"clientInfo", {{"name", "CloseCrab-Unified"}, {"version", "0.1.0"}}}
    };
    auto resp = sendRequest(init);
    if (resp.isError()) {
        spdlog::error("MCP init failed for '{}': {}", config_.name, resp.error.dump());
        disconnect();
        return false;
    }

    // Record what the server supports.
    parseServerCapabilities(resp.result.value("capabilities", nlohmann::json::object()));

    transport_->send(nlohmann::json({{"jsonrpc","2.0"},{"method","notifications/initialized"}}));
    spdlog::info("MCP: Connected to {} ({})", config_.name, config_.transport);
    return true;
}

void MCPClient::parseServerCapabilities(const nlohmann::json& c) {
    caps_ = {};
    if (c.contains("tools")) {
        caps_.tools = true;
        caps_.toolsListChanged = c["tools"].value("listChanged", false);
    }
    if (c.contains("resources")) {
        caps_.resources = true;
        caps_.resourcesSubscribe = c["resources"].value("subscribe", false);
        caps_.resourcesListChanged = c["resources"].value("listChanged", false);
    }
    if (c.contains("prompts")) {
        caps_.prompts = true;
        caps_.promptsListChanged = c["prompts"].value("listChanged", false);
    }
    caps_.logging = c.contains("logging");
    caps_.completions = c.contains("completions");
}

void MCPClient::disconnect() {
    if (!connected_) return;
    connected_ = false;
    readerRunning_ = false;
    if (transport_) transport_->stop();
    respCv_.notify_all();
    if (reader_.joinable()) reader_.join();
    if (transport_) transport_.reset();
}

void MCPClient::writeLine(const std::string& line) {
    if (!transport_) return;
    try {
        transport_->send(nlohmann::json::parse(line));
    } catch (...) {
        spdlog::warn("MCP: writeLine got non-JSON payload, dropped");
    }
}

void MCPClient::sendNotification(const std::string& method, const nlohmann::json& params) {
    if (!transport_ || !transport_->isOpen()) return;
    nlohmann::json n = {{"jsonrpc", "2.0"}, {"method", method}};
    if (!params.is_null()) n["params"] = params;
    transport_->send(n);
}

// Background reader: every inbound message is demultiplexed here.
//   - has "id" + ("result"|"error")  -> response to one of our requests
//   - has "id" + "method"            -> server->client request (we must reply)
//   - has "method", no "id"          -> notification
void MCPClient::readLoop() {
    while (readerRunning_) {
        nlohmann::json msg;
        if (!transport_ || !transport_->receive(msg, 1000)) {
            if (!readerRunning_) break;
            // timeout: loop again (lets us notice shutdown / closed transport)
            if (transport_ && !transport_->isOpen()) { break; }
            continue;
        }
        if (!msg.is_object()) continue;

        const bool hasId = msg.contains("id") && !msg["id"].is_null();
        const bool hasMethod = msg.contains("method");

        if (hasId && hasMethod) {
            handleServerRequest(msg);
        } else if (hasId) {
            int id = -1;
            try { id = msg["id"].get<int>(); } catch (...) { continue; }
            std::lock_guard<std::mutex> lk(respMutex_);
            responses_[id] = msg;
            respCv_.notify_all();
        } else if (hasMethod) {
            handleNotification(msg);
        }
    }
    // Reader exiting unexpectedly while still "connected" → transport dropped.
    if (connected_ && readerRunning_) {
        spdlog::warn("MCP: transport for '{}' closed; will reconnect on next use", config_.name);
        connected_ = false;
    }
}

MCPResponse MCPClient::sendRequest(const MCPRequest& req) {
    if (!transport_ || !transport_->isOpen())
        return {req.id, {}, {{"code", -1}, {"message", "Transport not open"}}};

    MCPRequest r = req;
    r.id = nextId_++;
    transport_->send(r.toJson());

    // Wait for the reader thread to deliver the matching response (30s budget).
    std::unique_lock<std::mutex> lk(respMutex_);
    bool got = respCv_.wait_for(lk, std::chrono::seconds(30), [&]() {
        return responses_.count(r.id) > 0 || !readerRunning_;
    });
    auto it = responses_.find(r.id);
    if (!got || it == responses_.end()) {
        responses_.erase(r.id);
        return {req.id, {}, {{"code", -1}, {"message", "Timeout waiting for response"}}};
    }
    nlohmann::json j = std::move(it->second);
    responses_.erase(it);
    lk.unlock();

    MCPResponse resp;
    resp.id = r.id;
    resp.result = j.value("result", nlohmann::json());
    resp.error = j.value("error", nlohmann::json());
    return resp;
}

// Server -> client request: sampling/createMessage, elicitation/create,
// roots/list, ping. We reply via the transport with a JSON-RPC result/error.
void MCPClient::handleServerRequest(const nlohmann::json& msg) {
    std::string method = msg.value("method", "");
    nlohmann::json params = msg.value("params", nlohmann::json::object());
    nlohmann::json id = msg["id"];

    auto reply = [&](const nlohmann::json& result) {
        transport_->send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
    };
    auto replyError = [&](int code, const std::string& message) {
        transport_->send({{"jsonrpc", "2.0"}, {"id", id},
                          {"error", {{"code", code}, {"message", message}}}});
    };

    try {
        if (method == "ping") {
            reply(nlohmann::json::object());
        } else if (method == "sampling/createMessage") {
            if (handlers_.onSampling) reply(handlers_.onSampling(params));
            else replyError(-32601, "sampling not supported");
        } else if (method == "elicitation/create") {
            if (handlers_.onElicitation) reply(handlers_.onElicitation(params));
            else replyError(-32601, "elicitation not supported");
        } else if (method == "roots/list") {
            if (handlers_.onListRoots) reply(handlers_.onListRoots());
            else replyError(-32601, "roots not supported");
        } else {
            replyError(-32601, "method not found: " + method);
        }
    } catch (const std::exception& e) {
        replyError(-32603, std::string("internal error: ") + e.what());
    }
}

void MCPClient::handleNotification(const nlohmann::json& msg) {
    std::string method = msg.value("method", "");
    nlohmann::json params = msg.value("params", nlohmann::json::object());
    spdlog::debug("MCP '{}' notification: {}", config_.name, method);
    if (handlers_.onNotification) {
        try { handlers_.onNotification(method, params); } catch (...) {}
    }
}

bool MCPClient::subscribeResource(const std::string& uri) {
    if (!caps_.resourcesSubscribe) return false;
    auto resp = sendRequest({"resources/subscribe", {{"uri", uri}}, 0});
    return !resp.isError();
}

bool MCPClient::unsubscribeResource(const std::string& uri) {
    if (!caps_.resourcesSubscribe) return false;
    auto resp = sendRequest({"resources/unsubscribe", {{"uri", uri}}, 0});
    return !resp.isError();
}

void MCPClient::attemptReconnect() {
    if (connected_) return;
    spdlog::info("MCP: reconnecting to '{}'...", config_.name);
    // Tear down any half-open state, then reconnect.
    readerRunning_ = false;
    if (transport_) transport_->stop();
    if (reader_.joinable()) reader_.join();
    if (transport_) transport_.reset();
    {
        std::lock_guard<std::mutex> lk(respMutex_);
        responses_.clear();
    }
    connect();
}

std::vector<MCPToolDef> MCPClient::listTools() {
    auto resp = sendRequest({"tools/list", {}, 0});
    std::vector<MCPToolDef> tools;
    if (!resp.isError() && resp.result.contains("tools")) {
        for (const auto& t : resp.result["tools"]) {
            tools.push_back({
                t.value("name", ""),
                t.value("description", ""),
                t.value("inputSchema", nlohmann::json::object())
            });
        }
    }
    return tools;
}

nlohmann::json MCPClient::callTool(const std::string& name, const nlohmann::json& args) {
    auto resp = sendRequest({"tools/call", {{"name", name}, {"arguments", args}}, 0});
    if (resp.isError()) return {{"error", resp.error}};
    return resp.result;
}

std::vector<MCPResource> MCPClient::listResources() {
    auto resp = sendRequest({"resources/list", {}, 0});
    std::vector<MCPResource> resources;
    if (!resp.isError() && resp.result.contains("resources")) {
        for (const auto& r : resp.result["resources"]) {
            resources.push_back({
                r.value("uri", ""), r.value("name", ""),
                r.value("mimeType", ""), r.value("description", "")
            });
        }
    }
    return resources;
}

std::string MCPClient::readResource(const std::string& uri) {
    auto resp = sendRequest({"resources/read", {{"uri", uri}}, 0});
    if (resp.isError()) return "[Error: " + resp.error.dump() + "]";
    if (resp.result.contains("contents") && resp.result["contents"].is_array() &&
        !resp.result["contents"].empty()) {
        return resp.result["contents"][0].value("text", "");
    }
    return resp.result.dump();
}

std::vector<nlohmann::json> MCPClient::listPrompts() {
    auto resp = sendRequest({"prompts/list", {}, 0});
    std::vector<nlohmann::json> prompts;
    if (!resp.isError() && resp.result.contains("prompts") && resp.result["prompts"].is_array()) {
        for (const auto& p : resp.result["prompts"]) prompts.push_back(p);
    }
    return prompts;
}

std::string MCPClient::getPrompt(const std::string& name, const nlohmann::json& args) {
    nlohmann::json params = {{"name", name}};
    if (!args.is_null() && !args.empty()) params["arguments"] = args;
    auto resp = sendRequest({"prompts/get", params, 0});
    if (resp.isError()) return "[Error: " + resp.error.dump() + "]";
    // Concatenate text from the rendered prompt messages.
    std::string out;
    if (resp.result.contains("messages") && resp.result["messages"].is_array()) {
        for (const auto& m : resp.result["messages"]) {
            const auto& content = m.value("content", nlohmann::json::object());
            if (content.is_object() && content.value("type", "") == "text") {
                out += content.value("text", "") + "\n";
            } else if (content.is_array()) {
                for (const auto& c : content) {
                    if (c.value("type", "") == "text") out += c.value("text", "") + "\n";
                }
            }
        }
    }
    return out.empty() ? resp.result.dump() : out;
}

// ---- MCPServerManager ----

MCPServerManager& MCPServerManager::getInstance() {
    static MCPServerManager instance;
    return instance;
}

bool MCPServerManager::addServer(const MCPServerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto client = std::make_unique<MCPClient>(config);

    // Install per-connection handlers. The notification handler is wrapped so a
    // tools/list_changed from this server invalidates the aggregated cache.
    MCPClientHandlers h = handlers_;
    auto userNotif = handlers_.onNotification;
    h.onNotification = [this, userNotif](const std::string& method, const nlohmann::json& params) {
        if (method == "notifications/tools/list_changed")
            invalidateToolCache();
        if (userNotif) userNotif(method, params);
    };
    client->setHandlers(h);

    if (!client->connect()) return false;
    clients_[config.name] = std::move(client);
    toolCacheValid_ = false;
    return true;
}

void MCPServerManager::setHandlers(const MCPClientHandlers& h) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_ = h;
    // Re-apply (wrapped) to already-connected clients.
    auto userNotif = handlers_.onNotification;
    for (auto& [name, client] : clients_) {
        MCPClientHandlers ch = handlers_;
        ch.onNotification = [this, userNotif](const std::string& method, const nlohmann::json& params) {
            if (method == "notifications/tools/list_changed")
                invalidateToolCache();
            if (userNotif) userNotif(method, params);
        };
        client->setHandlers(ch);
    }
}

void MCPServerManager::invalidateToolCache() {
    toolCacheValid_ = false;
}

void MCPServerManager::removeServer(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(name);
    if (it != clients_.end()) {
        it->second->disconnect();
        clients_.erase(it);
    }
}

MCPClient* MCPServerManager::getClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(name);
    if (it == clients_.end()) return nullptr;
    // Auto-recover a dropped connection before handing the client back.
    if (!it->second->isConnected()) it->second->ensureConnected();
    return it->second.get();
}

std::vector<std::string> MCPServerManager::getServerNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [n, _] : clients_) names.push_back(n);
    return names;
}

std::vector<std::pair<std::string, MCPToolDef>> MCPServerManager::getAllTools() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (toolCacheValid_) return toolCache_;
    std::vector<std::pair<std::string, MCPToolDef>> all;
    for (auto& [name, client] : clients_) {
        if (!client->isConnected()) continue;
        auto tools = client->listTools();
        for (auto& t : tools) all.push_back({name, std::move(t)});
    }
    toolCache_ = all;
    toolCacheValid_ = true;
    return all;
}

void MCPServerManager::loadFromSettings(const nlohmann::json& mcpServers) {
    for (auto& [name, cfg] : mcpServers.items()) {
        MCPServerConfig config;
        config.name = name;
        config.command = cfg.value("command", "");
        if (cfg.contains("args") && cfg["args"].is_array()) {
            for (const auto& a : cfg["args"]) config.args.push_back(a.get<std::string>());
        }
        if (cfg.contains("env") && cfg["env"].is_object()) {
            for (auto& [k, v] : cfg["env"].items()) config.env[k] = v.get<std::string>();
        }
        // Transport channel. Accept both our "transport" key and JackProAi's
        // "type" key (stdio | sse | http | streamable-http); default stdio.
        config.transport = cfg.value("transport", cfg.value("type", "stdio"));
        config.url = cfg.value("url", "");
        if (cfg.contains("headers") && cfg["headers"].is_object()) {
            for (auto& [k, v] : cfg["headers"].items()) config.headers[k] = v.get<std::string>();
        }
        // OAuth block: { clientId, authUrl, tokenUrl, scope }.
        if (cfg.contains("oauth") && cfg["oauth"].is_object()) {
            const auto& o = cfg["oauth"];
            config.oauth = true;
            config.oauthClientId = o.value("clientId", "");
            config.oauthClientSecret = o.value("clientSecret", "");
            config.oauthAuthUrl  = o.value("authUrl", "");
            config.oauthTokenUrl = o.value("tokenUrl", "");
            config.oauthScope    = o.value("scope", "");
        }

        spdlog::info("MCP: Loading server '{}'...", name);
        if (!addServer(config)) {
            spdlog::warn("MCP: Failed to connect to '{}'", name);
        }
    }
}

void MCPServerManager::disconnectAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [_, client] : clients_) client->disconnect();
    clients_.clear();
}

} // namespace closecrab
