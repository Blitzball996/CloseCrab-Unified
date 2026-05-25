#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <functional>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXWebSocketServer.h>
#include <spdlog/spdlog.h>

namespace closecrab {

class MobileWebSocket {
public:
    using MessageHandler = std::function<void(const std::string& clientId, const std::string& type, const nlohmann::json& data)>;

    static MobileWebSocket& getInstance() {
        static MobileWebSocket instance;
        return instance;
    }

    void start(int port = 9002) {
        if (running_) return;
        server_ = std::make_unique<ix::WebSocketServer>(port, "0.0.0.0");

        server_->setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState> state,
            ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
            if (msg->type == ix::WebSocketMessageType::Open) {
                std::lock_guard<std::mutex> lock(mutex_);
                std::string cid = std::to_string(reinterpret_cast<uintptr_t>(&ws));
                clients_.push_back(&ws);
                clientIdMap_[cid] = &ws;
                wsToClientId_[&ws] = cid;
                spdlog::info("Mobile client connected: {} (total: {})", cid, clients_.size());
                // Send client their ID
                nlohmann::json welcome = {{"type", "connected"}, {"clientId", cid}};
                ws.send(welcome.dump());
            } else if (msg->type == ix::WebSocketMessageType::Close) {
                std::lock_guard<std::mutex> lock(mutex_);
                std::string cid = wsToClientId_[&ws];
                clients_.erase(std::remove(clients_.begin(), clients_.end(), &ws), clients_.end());
                clientIdMap_.erase(cid);
                wsToClientId_.erase(&ws);
                spdlog::info("Mobile client disconnected: {} (total: {})", cid, clients_.size());
            } else if (msg->type == ix::WebSocketMessageType::Message) {
                std::string cid;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    cid = wsToClientId_[&ws];
                }
                handleIncoming(cid, msg->str);
            }
        });

        auto res = server_->listen();
        if (!res.first) {
            spdlog::error("MobileWebSocket failed to listen on port {}: {}", port, res.second);
            return;
        }
        server_->start();
        running_ = true;
        spdlog::info("MobileWebSocket started on port {}", port);
    }

    void stop() {
        if (!running_) return;
        server_->stop();
        running_ = false;
    }

    // === Per-client send (Team Mode) ===
    void sendToClient(const std::string& clientId, const nlohmann::json& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clientIdMap_.find(clientId);
        if (it != clientIdMap_.end()) {
            it->second->send(msg.dump());
        }
    }

    void sendTextToClient(const std::string& clientId, const std::string& text) {
        sendToClient(clientId, {{"type", "text"}, {"content", text}});
    }

    void sendToolUseToClient(const std::string& clientId, const std::string& toolName, const std::string& desc) {
        sendToClient(clientId, {{"type", "tool_use"}, {"tool", toolName}, {"description", desc}});
    }

    void sendCompleteToClient(const std::string& clientId) {
        sendToClient(clientId, {{"type", "complete"}});
    }

    void sendErrorToClient(const std::string& clientId, const std::string& error) {
        sendToClient(clientId, {{"type", "error"}, {"content", error}});
    }

    // === Broadcast (legacy, still works for single-user mode) ===
    void sendText(const std::string& text) {
        broadcast({{"type", "text"}, {"content", text}});
    }

    void sendToolUse(const std::string& toolName, const std::string& description) {
        broadcast({{"type", "tool_use"}, {"tool", toolName}, {"description", description}});
    }

    void sendToolResult(const std::string& toolName, bool success, double elapsed) {
        broadcast({{"type", "tool_result"}, {"tool", toolName}, {"success", success}, {"elapsed", elapsed}});
    }

    void sendPermissionRequest(const std::string& toolName, const std::string& description, const std::string& requestId) {
        broadcast({{"type", "permission_request"}, {"tool", toolName}, {"description", description}, {"request_id", requestId}});
    }

    void sendError(const std::string& error) {
        broadcast({{"type", "error"}, {"content", error}});
    }

    void sendComplete() {
        broadcast({{"type", "complete"}});
    }

    void setMessageHandler(MessageHandler handler) {
        messageHandler_ = handler;
    }

    bool hasClients() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !clients_.empty();
    }

    int clientCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return (int)clients_.size();
    }

    std::string getClientId(ix::WebSocket* ws) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = wsToClientId_.find(ws);
        return it != wsToClientId_.end() ? it->second : "";
    }

private:
    MobileWebSocket() = default;

    void broadcast(const nlohmann::json& msg) {
        std::string data = msg.dump();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* client : clients_) {
            client->send(data);
        }
    }

    void handleIncoming(const std::string& clientId, const std::string& raw) {
        try {
            auto msg = nlohmann::json::parse(raw);
            std::string type = msg.value("type", "");
            if (messageHandler_) {
                messageHandler_(clientId, type, msg);
            }
        } catch (...) {}
    }

    mutable std::mutex mutex_;
    std::unique_ptr<ix::WebSocketServer> server_;
    std::vector<ix::WebSocket*> clients_;
    std::map<std::string, ix::WebSocket*> clientIdMap_;  // clientId → ws
    std::map<ix::WebSocket*, std::string> wsToClientId_; // ws → clientId
    MessageHandler messageHandler_;
    bool running_ = false;
};

} // namespace closecrab
