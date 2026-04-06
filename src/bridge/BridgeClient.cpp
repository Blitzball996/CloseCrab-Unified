#include "BridgeClient.h"
#include <spdlog/spdlog.h>
#include <curl/curl.h>

namespace closecrab {

bool BridgeClient::connect() {
    // Simplified: HTTP-based bridge
    spdlog::info("Bridge: Connecting to {}", config_.serverUrl);
    connected_ = true;
    return true;
}

void BridgeClient::disconnect() {
    running_ = false;
    connected_ = false;
    if (receiveThread_ && receiveThread_->joinable()) receiveThread_->join();
}

void BridgeClient::sendMessage(const std::string& sessionId, const std::string& message) {
    if (!connected_) return;

    CURL* curl = curl_easy_init();
    if (!curl) return;

    nlohmann::json body = {{"session_id", sessionId}, {"message", message}};
    std::string bodyStr = body.dump();
    std::string url = config_.serverUrl + "/chat";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!config_.authToken.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + config_.authToken).c_str());
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* p, size_t s, size_t n, void* u) -> size_t {
        static_cast<std::string*>(u)->append(p, s * n); return s * n;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && callback_) {
        try { callback_(nlohmann::json::parse(response)); } catch (...) {}
    }
}

std::string BridgeClient::createSession() {
    // Would create a session on the remote server
    return "remote_session_1";
}

bool BridgeServer::start(int port) {
    spdlog::info("Bridge server starting on port {}", port);
    running_ = true;
    // Full implementation would use HttpServer
    return true;
}

void BridgeServer::stop() {
    running_ = false;
}

} // namespace closecrab
