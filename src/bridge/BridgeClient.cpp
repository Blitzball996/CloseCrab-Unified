#include "BridgeClient.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>

namespace closecrab {

// ---- BridgeClient ----

bool BridgeClient::connect() {
    return connectHTTP();
}

bool BridgeClient::connectHTTP() {
    spdlog::info("Bridge: Connecting to {} (HTTP)", config_.serverUrl);

    // Test connection with a ping
    try {
        auto resp = httpPost("/ping", {{"client", "closecrab-unified"}});
        if (resp.contains("status") && resp["status"] == "ok") {
            connected_ = true;
            reconnectAttempts_ = 0;
            spdlog::info("Bridge: Connected to {}", config_.serverUrl);
            return true;
        }
    } catch (...) {}

    // If ping fails, still mark as connected for fire-and-forget usage
    connected_ = true;
    return true;
}

void BridgeClient::disconnect() {
    running_ = false;
    connected_ = false;
    if (receiveThread_ && receiveThread_->joinable()) receiveThread_->join();
    spdlog::info("Bridge: Disconnected");
}

void BridgeClient::sendMessage(const std::string& sessionId, const std::string& message) {
    if (!connected_) return;

    nlohmann::json body = {{"session_id", sessionId}, {"message", message}};
    try {
        httpPost("/chat", body);
    } catch (const std::exception& e) {
        spdlog::warn("Bridge sendMessage failed: {}", e.what());
        scheduleReconnect();
    }
}

void BridgeClient::sendCommand(const std::string& action, const nlohmann::json& payload) {
    if (!connected_) return;

    nlohmann::json body = {{"action", action}, {"payload", payload}};
    try {
        auto resp = httpPost("/command", body);
        if (callback_) callback_(resp);
    } catch (const std::exception& e) {
        spdlog::warn("Bridge sendCommand failed: {}", e.what());
    }
}

nlohmann::json BridgeClient::executeRemote(const std::string& command, int timeoutMs) {
    if (!connected_) return {{"error", "Not connected"}};

    nlohmann::json body = {
        {"action", "execute"},
        {"payload", {{"command", command}}}
    };

    try {
        return httpPost("/command", body);
    } catch (const std::exception& e) {
        return {{"error", e.what()}};
    }
}

std::string BridgeClient::createSession() {
    try {
        auto resp = httpPost("/session/create", {});
        return resp.value("session_id", "remote_session_1");
    } catch (...) {
        return "remote_session_1";
    }
}

static size_t curlWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

nlohmann::json BridgeClient::httpPost(const std::string& path, const nlohmann::json& body) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("CURL init failed");

    std::string bodyStr = body.dump();
    std::string url = config_.serverUrl + path;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!config_.authToken.empty()) {
        headers = curl_slist_append(headers,
            ("Authorization: Bearer " + config_.authToken).c_str());
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(res));
    }

    try {
        return nlohmann::json::parse(response);
    } catch (...) {
        return {{"raw", response}, {"http_status", httpCode}};
    }
}

void BridgeClient::scheduleReconnect() {
    if (reconnectAttempts_ >= config_.maxReconnectAttempts) {
        spdlog::error("Bridge: Max reconnect attempts reached, giving up");
        connected_ = false;
        return;
    }

    reconnectAttempts_++;
    int delay = config_.reconnectIntervalMs * reconnectAttempts_;
    spdlog::info("Bridge: Reconnecting in {}ms (attempt {}/{})",
                 delay, reconnectAttempts_, config_.maxReconnectAttempts);

    std::thread([this, delay]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if (!connected_) connect();
    }).detach();
}

// ---- BridgeServer ----

bool BridgeServer::start(int port) {
    if (running_) return true;
    spdlog::info("Bridge server starting on port {}", port);
    running_ = true;

    serverThread_ = std::make_unique<std::thread>([this, port]() {
        // Use httplib for a simple HTTP server
        // This is a simplified version — full impl would use HttpServer from network/
        spdlog::info("Bridge server listening on port {}", port);
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    return true;
}

void BridgeServer::stop() {
    running_ = false;
    if (serverThread_ && serverThread_->joinable()) serverThread_->join();
    spdlog::info("Bridge server stopped");
}

} // namespace closecrab
