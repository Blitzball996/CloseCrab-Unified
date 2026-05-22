#pragma once
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace closecrab {

struct RemoteMessage {
    std::string type;       // "prompt", "response", "tool_use", "tool_result", "status"
    std::string sessionId;
    nlohmann::json payload;
    long long timestamp = 0;
};

class RemoteSession {
public:
    using MessageHandler = std::function<void(const RemoteMessage&)>;

    static RemoteSession& getInstance() {
        static RemoteSession instance;
        return instance;
    }

    // Start accepting remote connections on a port
    bool start(int port = 9002, const std::string& authToken = "") {
        if (running_) return false;
        port_ = port;
        authToken_ = authToken;
        running_ = true;
        spdlog::info("Remote session server started on port {}", port);
        // Actual WebSocket server implementation would go here
        // using the existing WebSocketServer from src/network/WebSocketServer.cpp
        return true;
    }

    void stop() {
        running_ = false;
        spdlog::info("Remote session server stopped");
    }

    bool isRunning() const { return running_; }
    int getPort() const { return port_; }

    // Send a message to all connected clients
    void broadcast(const RemoteMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        // Would serialize and send via WebSocket
        nlohmann::json j;
        j["type"] = msg.type;
        j["session_id"] = msg.sessionId;
        j["payload"] = msg.payload;
        j["timestamp"] = msg.timestamp;
        lastBroadcast_ = j.dump();
        broadcastCount_++;
    }

    // Register handler for incoming messages
    void onMessage(MessageHandler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handler_ = std::move(handler);
    }

    // Handle an incoming message (called by WebSocket server)
    void handleIncoming(const std::string& raw) {
        try {
            auto j = nlohmann::json::parse(raw);
            RemoteMessage msg;
            msg.type = j.value("type", "");
            msg.sessionId = j.value("session_id", "");
            msg.payload = j.value("payload", nlohmann::json::object());
            msg.timestamp = j.value("timestamp", (long long)0);

            // Auth check
            if (!authToken_.empty() && j.value("auth", "") != authToken_) {
                spdlog::warn("Remote session: unauthorized message rejected");
                return;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            if (handler_) handler_(msg);
        } catch (const std::exception& e) {
            spdlog::warn("Remote session: failed to parse message: {}", e.what());
        }
    }

    // Stats
    int getBroadcastCount() const { return broadcastCount_; }
    std::string getConnectionInfo() const {
        if (!running_) return "Not running";
        return "ws://localhost:" + std::to_string(port_) + " (auth: " +
               (authToken_.empty() ? "none" : "token") + ")";
    }

private:
    RemoteSession() = default;
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    int port_ = 9002;
    std::string authToken_;
    MessageHandler handler_;
    std::string lastBroadcast_;
    int broadcastCount_ = 0;
};

} // namespace closecrab
