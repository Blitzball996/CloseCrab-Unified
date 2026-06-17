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
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXWebSocketServer.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <process.h>   // _getpid
#define CC_GETPID _getpid
#else
#include <unistd.h>    // getpid
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define CC_GETPID getpid
#endif

namespace closecrab {

class MobileWebSocket {
public:
    using MessageHandler = std::function<void(const std::string& clientId, const std::string& type, const nlohmann::json& data)>;

    static MobileWebSocket& getInstance() {
        static MobileWebSocket instance;
        return instance;
    }

    // Helper: test if port is truly free (work around SO_REUSEADDR letting multiple bind to same port)
    static bool isPortFree(int port) {
#ifdef _WIN32
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        // Try to connect: if succeeds, someone is listening = port busy
        bool busy = (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0);
        closesocket(sock);
        return !busy;  // free if connect failed
#else
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        bool busy = (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0);
        close(sock);
        return !busy;
#endif
    }

    void start(int basePort = 9002) {
        if (running_) return;

        // Try ports basePort..basePort+19; each CloseCrab window gets its own.
        int port = basePort;
        bool bound = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            // Check port is truly free before creating server (avoids SO_REUSEADDR trap)
            if (!isPortFree(port)) {
                spdlog::warn("MobileWebSocket port {} busy (connect test), trying next...", port);
                port++;
                continue;
            }

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
        if (res.first) {
            bound = true;
            break;
        }
            // listen() failed despite passing connect test - likely race, try next
            spdlog::warn("MobileWebSocket port {} listen failed, trying next...", port);
            port++;
            server_.reset();
        } // end for
        if (!bound) {
            spdlog::error("MobileWebSocket: no free port in range {}-{}", basePort, basePort + 19);
            return;
        }
        server_->start();
        running_ = true;
        port_ = port;

        // Write a discovery file so CloseCrab-Web can find all running windows.
        writePortFile();
        spdlog::info("MobileWebSocket started on port {}", port);
    }

    void stop() {
        if (!running_) return;
        server_->stop();
        running_ = false;
        removePortFile();
    }

    int getPort() const { return port_; }

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
    int port_ = 0;
    std::string portFilePath_;

    // Write data/mobile-ws-port-<PID>.json so CloseCrab-Web can find all running windows.
    void writePortFile() {
        namespace fsys = std::filesystem;
        auto dir = fsys::current_path() / "data";
        if (!fsys::exists(dir)) { try { fsys::create_directories(dir); } catch (...) {} }
        int pid = CC_GETPID();
        portFilePath_ = (dir / ("mobile-ws-port-" + std::to_string(pid) + ".json")).string();
        try {
            nlohmann::json j = {{"pid", pid}, {"port", port_}};
            std::ofstream(portFilePath_) << j.dump();
        } catch (const std::exception& e) {
            spdlog::warn("Could not write port file: {}", e.what());
        }
    }

    void removePortFile() {
        if (!portFilePath_.empty()) {
            try { std::filesystem::remove(portFilePath_); } catch (...) {}
            portFilePath_.clear();
        }
    }

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
