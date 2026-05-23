#include "HttpServer.h"
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <fstream>
#include <vector>

using json = nlohmann::json;

class HttpServer::Impl {
public:
    int port;
    std::unique_ptr<httplib::Server> server;
    std::atomic<bool> running{ false };
    std::thread serverThread;
    std::function<std::string(const std::string&, const std::string&)> chatCallback;

    Impl(int p) : port(p) {}
};

HttpServer::HttpServer(int port) : pImpl(std::make_unique<Impl>(port)) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::onChat(std::function<std::string(const std::string&, const std::string&)> callback) {
    pImpl->chatCallback = callback;
}

void HttpServer::start() {
    if (pImpl->running) return;

    pImpl->server = std::make_unique<httplib::Server>();

    // Health check
    pImpl->server->Get("/health", [](const httplib::Request& req, httplib::Response& res) {
        json resp = {
            {"status", "ok"},
            {"service", "CloseCrab"},
            {"version", "1.0.0"}
        };
        res.set_content(resp.dump(), "application/json");
        });

    // Mobile remote control page
    pImpl->server->Get("/mobile", [](const httplib::Request& req, httplib::Response& res) {
        std::vector<std::string> paths = {
            "extensions/mobile-web/index.html",
            "../extensions/mobile-web/index.html",
            "../../extensions/mobile-web/index.html"
        };
        for (const auto& path : paths) {
            std::ifstream f(path);
            if (f.is_open()) {
                std::string html((std::istreambuf_iterator<char>(f)), {});
                res.set_content(html, "text/html");
                return;
            }
        }
        res.set_content("<html><body><h1>CloseCrab Mobile</h1><p>mobile-web/index.html not found</p></body></html>", "text/html");
    });

    // Chat API
    pImpl->server->Post("/chat", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            // (comment)
            json body = json::parse(req.body);

            std::string message = body.value("message", "");
            std::string sessionId = body.value("session_id", "");
            bool stream = body.value("stream", false);

            if (message.empty()) {
                json error = { {"error", "message is required"} };
                res.status = 400;
                res.set_content(error.dump(), "application/json");
                return;
            }

            spdlog::info("HTTP API request: {}", message);

            // (comment)
            if (pImpl->chatCallback) {
                std::string response = pImpl->chatCallback(message, sessionId);

                json resp = {
                    {"response", response},
                    {"session_id", sessionId.empty() ? "new_session" : sessionId}
                };
                res.set_content(resp.dump(), "application/json");
            }
            else {
                json error = { {"error", "chat callback not set"} };
                res.status = 500;
                res.set_content(error.dump(), "application/json");
            }

        }
        catch (const json::parse_error& e) {
            json error = { {"error", "Invalid JSON"}, {"detail", e.what()} };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
        catch (const std::exception& e) {
            json error = { {"error", e.what()} };
            res.status = 500;
            res.set_content(error.dump(), "application/json");
        }
        });

    // (comment)
    pImpl->server->Get("/history/:session_id", [](const httplib::Request& req, httplib::Response& res) {
        // (comment)
        json resp = { {"message", "history endpoint - to be implemented"} };
        res.set_content(resp.dump(), "application/json");
        });

    // (comment)
    pImpl->server->Get("/skills", [](const httplib::Request& req, httplib::Response& res) {
        // (comment)
        json resp = { {"message", "skills endpoint - to be implemented"} };
        res.set_content(resp.dump(), "application/json");
        });

    pImpl->running = true;
    pImpl->serverThread = std::thread([this]() {
        spdlog::info("HTTP server starting on port {}", pImpl->port);
        pImpl->server->listen("0.0.0.0", pImpl->port);
        });
}

void HttpServer::stop() {
    if (!pImpl->running) return;

    if (pImpl->server) {
        pImpl->server->stop();
    }

    if (pImpl->serverThread.joinable()) {
        pImpl->serverThread.join();
    }

    pImpl->running = false;
    spdlog::info("HTTP server stopped");
}

bool HttpServer::isRunning() const {
    return pImpl->running;
}

int HttpServer::getPort() const {
    return pImpl->port;
}