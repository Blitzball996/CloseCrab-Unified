#pragma once

#include <string>
#include <map>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <httplib.h>

namespace closecrab {

// Server-Sent Events server for streaming responses to web clients
class SSEServer {
public:
    explicit SSEServer(int port = 8081);
    ~SSEServer();

    void start();
    void stop();
    bool isRunning() const { return running_; }
    int getPort() const { return port_; }

    // Send an event to a specific client
    void sendEvent(const std::string& clientId, const std::string& event, const std::string& data);

    // Send an event to all connected clients
    void broadcast(const std::string& event, const std::string& data);

    // Callbacks
    void onConnect(std::function<void(const std::string& clientId)> cb) { onConnect_ = std::move(cb); }
    void onDisconnect(std::function<void(const std::string& clientId)> cb) { onDisconnect_ = std::move(cb); }
    void onMessage(std::function<void(const std::string& clientId, const std::string& message)> cb) {
        onMessage_ = std::move(cb);
    }

private:
    int port_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> serverThread_;

    struct ClientConnection {
        std::string id;
        httplib::DataSink* sink = nullptr;
        bool alive = true;
    };

    mutable std::mutex clientsMutex_;
    std::map<std::string, std::shared_ptr<ClientConnection>> clients_;
    int nextClientId_ = 0;

    std::function<void(const std::string&)> onConnect_;
    std::function<void(const std::string&)> onDisconnect_;
    std::function<void(const std::string&, const std::string&)> onMessage_;

    std::string generateClientId();
};

} // namespace closecrab
