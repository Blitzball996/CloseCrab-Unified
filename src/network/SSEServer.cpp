#include "SSEServer.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {

SSEServer::SSEServer(int port) : port_(port) {}

SSEServer::~SSEServer() { stop(); }

std::string SSEServer::generateClientId() {
    return "sse_" + std::to_string(++nextClientId_);
}

void SSEServer::start() {
    if (running_) return;
    running_ = true;

    serverThread_ = std::make_unique<std::thread>([this]() {
        httplib::Server svr;

        // SSE endpoint — clients connect here for streaming
        svr.Get("/events", [this](const httplib::Request& req, httplib::Response& res) {
            std::string clientId = generateClientId();

            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
            res.set_header("Access-Control-Allow-Origin", "*");

            res.set_chunked_content_provider("text/event-stream",
                [this, clientId](size_t offset, httplib::DataSink& sink) -> bool {
                    // Register client
                    {
                        std::lock_guard<std::mutex> lock(clientsMutex_);
                        auto conn = std::make_shared<ClientConnection>();
                        conn->id = clientId;
                        conn->sink = &sink;
                        conn->alive = true;
                        clients_[clientId] = conn;
                    }

                    if (onConnect_) onConnect_(clientId);

                    // Send initial connection event
                    std::string initEvent = "event: connected\ndata: {\"clientId\":\"" + clientId + "\"}\n\n";
                    sink.write(initEvent.c_str(), initEvent.size());

                    // Keep connection alive until client disconnects
                    while (running_) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));

                        std::lock_guard<std::mutex> lock(clientsMutex_);
                        auto it = clients_.find(clientId);
                        if (it == clients_.end() || !it->second->alive) break;
                    }

                    // Cleanup
                    {
                        std::lock_guard<std::mutex> lock(clientsMutex_);
                        clients_.erase(clientId);
                    }
                    if (onDisconnect_) onDisconnect_(clientId);

                    return false; // End chunked response
                }
            );
        });

        // POST endpoint for sending messages
        svr.Post("/chat", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = nlohmann::json::parse(req.body);
                std::string clientId = body.value("client_id", "");
                std::string message = body.value("message", "");

                if (onMessage_) onMessage_(clientId, message);

                res.set_content("{\"status\":\"ok\"}", "application/json");
            } catch (const std::exception& e) {
                res.status = 400;
                res.set_content("{\"error\":\"" + std::string(e.what()) + "\"}", "application/json");
            }
        });

        // Health check
        svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("{\"status\":\"ok\",\"type\":\"sse\"}", "application/json");
        });

        spdlog::info("SSE Server starting on port {}", port_);
        svr.listen("0.0.0.0", port_);
    });
}

void SSEServer::stop() {
    if (!running_) return;
    running_ = false;

    // Mark all clients as dead
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& [_, conn] : clients_) conn->alive = false;
    }

    if (serverThread_ && serverThread_->joinable()) {
        serverThread_->join();
    }
}

void SSEServer::sendEvent(const std::string& clientId, const std::string& event, const std::string& data) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    if (it == clients_.end() || !it->second->sink) return;

    std::string msg = "event: " + event + "\ndata: " + data + "\n\n";
    it->second->sink->write(msg.c_str(), msg.size());
}

void SSEServer::broadcast(const std::string& event, const std::string& data) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    std::string msg = "event: " + event + "\ndata: " + data + "\n\n";

    for (auto& [_, conn] : clients_) {
        if (conn->sink && conn->alive) {
            conn->sink->write(msg.c_str(), msg.size());
        }
    }
}

} // namespace closecrab
