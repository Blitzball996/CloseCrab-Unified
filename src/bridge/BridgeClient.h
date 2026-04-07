#pragma once

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace closecrab {

// Bridge client — connects to a remote CloseCrab instance via WebSocket or HTTP
class BridgeClient {
public:
    struct Config {
        std::string serverUrl;
        std::string authToken;
        bool useWebSocket = true;
        int reconnectIntervalMs = 3000;
        int maxReconnectAttempts = 10;
    };

    using MessageCallback = std::function<void(const nlohmann::json&)>;

    explicit BridgeClient(const Config& config) : config_(config) {}
    ~BridgeClient() { disconnect(); }

    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    // Send a message to remote
    void sendMessage(const std::string& sessionId, const std::string& message);

    // Send a structured command
    void sendCommand(const std::string& action, const nlohmann::json& payload = {});

    // Execute a command on remote and wait for result
    nlohmann::json executeRemote(const std::string& command, int timeoutMs = 30000);

    // Set callback for incoming messages
    void onMessage(MessageCallback cb) { callback_ = std::move(cb); }

    // Create a remote session
    std::string createSession();

private:
    // HTTP fallback methods
    bool connectHTTP();
    nlohmann::json httpPost(const std::string& path, const nlohmann::json& body);

    // Reconnection logic
    void scheduleReconnect();

    Config config_;
    bool connected_ = false;
    MessageCallback callback_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> receiveThread_;

    // Pending request/response for executeRemote
    std::mutex responseMutex_;
    std::condition_variable responseCv_;
    nlohmann::json pendingResponse_;
    bool responseReady_ = false;

    int reconnectAttempts_ = 0;
};

// Bridge server — accepts remote connections and processes commands
class BridgeServer {
public:
    using CommandHandler = std::function<nlohmann::json(const std::string& action,
                                                         const nlohmann::json& payload)>;

    bool start(int port = 9002);
    void stop();
    bool isRunning() const { return running_; }

    void setCommandHandler(CommandHandler handler) { handler_ = std::move(handler); }

private:
    std::atomic<bool> running_{false};
    CommandHandler handler_;
    std::unique_ptr<std::thread> serverThread_;
};

} // namespace closecrab
