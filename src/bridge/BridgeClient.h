#pragma once

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>

namespace closecrab {

// Bridge client — connects to a remote CloseCrab instance
class BridgeClient {
public:
    struct Config {
        std::string serverUrl;
        std::string authToken;
        bool useWebSocket = true;
    };

    using MessageCallback = std::function<void(const nlohmann::json&)>;

    explicit BridgeClient(const Config& config) : config_(config) {}
    ~BridgeClient() { disconnect(); }

    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    // Send a message to remote
    void sendMessage(const std::string& sessionId, const std::string& message);

    // Set callback for incoming messages
    void onMessage(MessageCallback cb) { callback_ = std::move(cb); }

    // Create a remote session
    std::string createSession();

private:
    Config config_;
    bool connected_ = false;
    MessageCallback callback_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> receiveThread_;
};

// Bridge server — accepts remote connections
class BridgeServer {
public:
    bool start(int port = 9002);
    void stop();

private:
    std::atomic<bool> running_{false};
};

} // namespace closecrab
