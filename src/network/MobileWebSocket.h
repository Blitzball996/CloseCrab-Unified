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
    using MessageHandler = std::function<void(const std::string& type, const nlohmann::json& data)>;

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
                clients_.push_back(&ws);
                spdlog::info("Mobile client connected (total: {})", clients_.size());
            } else if (msg->type == ix::WebSocketMessageType::Close) {
                std::lock_guard<std::mutex> lock(mutex_);
                clients_.erase(std::remove(clients_.begin(), clients_.end(), &ws), clients_.end());
                spdlog::info("Mobile client disconnected (total: {})", clients_.size());
            } else if (msg->type == ix::WebSocketMessageType::Message) {
                handleIncoming(msg->str);
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

    // Broadcast events to all connected mobile clients
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

    // Set handler for incoming messages from mobile
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

private:
    MobileWebSocket() = default;

    void broadcast(const nlohmann::json& msg) {
        std::string data = msg.dump();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* client : clients_) {
            client->send(data);
        }
    }

    void handleIncoming(const std::string& raw) {
        try {
            auto msg = nlohmann::json::parse(raw);
            std::string type = msg.value("type", "");
            if (messageHandler_) {
                messageHandler_(type, msg);
            }
        } catch (...) {}
    }

    mutable std::mutex mutex_;
    std::unique_ptr<ix::WebSocketServer> server_;
    std::vector<ix::WebSocket*> clients_;
    MessageHandler messageHandler_;
    bool running_ = false;
};

} // namespace closecrab
