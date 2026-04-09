#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <curl/curl.h>

namespace closecrab {

// Abstract transport layer for Bridge communication
class BridgeTransport {
public:
    virtual ~BridgeTransport() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual void send(const nlohmann::json& msg) = 0;
    virtual void onMessage(std::function<void(const nlohmann::json&)> cb) = 0;
    virtual std::string name() const = 0;
};

// Helper: HTTP POST via libcurl (shared by both transports)
namespace detail {

static size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

inline bool httpPost(const std::string& url, const std::string& body, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        spdlog::warn("HTTP POST to {} failed: {}", url, curl_easy_strerror(res));
        return false;
    }
    return true;
}

inline bool httpGet(const std::string& url, std::string& response) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        spdlog::warn("HTTP GET {} failed: {}", url, curl_easy_strerror(res));
        return false;
    }
    return true;
}

} // namespace detail

// v1: Hybrid Transport — HTTP POST primary (WebSocket upgrade when available)
class HybridTransport : public BridgeTransport {
public:
    HybridTransport(const std::string& wsUrl, const std::string& httpUrl)
        : wsUrl_(wsUrl), httpUrl_(httpUrl) {}

    std::string name() const override { return "hybrid-v1"; }

    bool connect() override {
        // Verify the HTTP endpoint is reachable
        std::string response;
        connected_ = detail::httpGet(httpUrl_ + "/health", response);
        if (!connected_) {
            // Try without /health path
            connected_ = detail::httpPost(httpUrl_, "{\"type\":\"ping\"}", response);
        }
        if (connected_) {
            spdlog::info("HybridTransport connected to {}", httpUrl_);
            // Start polling thread for incoming messages
            pollThread_ = std::thread([this]() { pollLoop(); });
            pollThread_.detach();
        } else {
            spdlog::error("HybridTransport failed to connect to {}", httpUrl_);
        }
        return connected_;
    }

    void disconnect() override {
        connected_ = false;
        spdlog::info("HybridTransport disconnected");
    }

    bool isConnected() const override { return connected_; }

    void send(const nlohmann::json& msg) override {
        if (!connected_) return;
        std::string body = msg.dump();
        std::string response;
        if (!detail::httpPost(httpUrl_ + "/send", body, response)) {
            spdlog::warn("HybridTransport send failed");
        }
    }

    void onMessage(std::function<void(const nlohmann::json&)> cb) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        messageCallback_ = std::move(cb);
    }

private:
    void pollLoop() {
        while (connected_) {
            std::string response;
            if (detail::httpGet(httpUrl_ + "/poll", response) && !response.empty()) {
                try {
                    auto msg = nlohmann::json::parse(response);
                    std::lock_guard<std::mutex> lock(cbMutex_);
                    if (messageCallback_) messageCallback_(msg);
                } catch (...) {}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    std::string wsUrl_;
    std::string httpUrl_;
    std::atomic<bool> connected_{false};
    std::mutex cbMutex_;
    std::function<void(const nlohmann::json&)> messageCallback_;
    std::thread pollThread_;
};

// v2: SSE Transport — SSE for receiving + HTTP POST for sending
class SSETransport : public BridgeTransport {
public:
    SSETransport(const std::string& sseUrl, const std::string& postUrl)
        : sseUrl_(sseUrl), postUrl_(postUrl) {}

    std::string name() const override { return "sse-v2"; }

    bool connect() override {
        connected_ = true;
        // Start SSE listener in background thread using curl streaming
        sseThread_ = std::thread([this]() {
            spdlog::info("SSE listener started for {}", sseUrl_);

            CURL* curl = curl_easy_init();
            if (!curl) { connected_ = false; return; }

            curl_easy_setopt(curl, CURLOPT_URL, sseUrl_.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sseWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); // No timeout for SSE
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

            // SSE: set Accept header
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Accept: text/event-stream");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            CURLcode res = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK && connected_) {
                spdlog::warn("SSE connection ended: {}", curl_easy_strerror(res));
            }
            connected_ = false;
        });
        sseThread_.detach();
        return true;
    }

    void disconnect() override {
        connected_ = false;
        spdlog::info("SSETransport disconnected");
    }

    bool isConnected() const override { return connected_; }

    void send(const nlohmann::json& msg) override {
        if (!connected_) return;
        std::string body = msg.dump();
        std::string response;
        if (!detail::httpPost(postUrl_, body, response)) {
            spdlog::warn("SSETransport send failed");
        }
    }

    void onMessage(std::function<void(const nlohmann::json&)> cb) override {
        std::lock_guard<std::mutex> lock(cbMutex_);
        messageCallback_ = std::move(cb);
    }

private:
    // SSE stream parser callback
    static size_t sseWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* self = static_cast<SSETransport*>(userdata);
        if (!self->connected_) return 0; // Abort

        std::string chunk(ptr, size * nmemb);
        self->sseBuffer_ += chunk;

        // Parse SSE events: "data: {...}\n\n"
        size_t pos;
        while ((pos = self->sseBuffer_.find("\n\n")) != std::string::npos) {
            std::string event = self->sseBuffer_.substr(0, pos);
            self->sseBuffer_ = self->sseBuffer_.substr(pos + 2);

            // Extract data field
            std::string dataPrefix = "data: ";
            auto dataPos = event.find(dataPrefix);
            if (dataPos != std::string::npos) {
                std::string data = event.substr(dataPos + dataPrefix.size());
                try {
                    auto msg = nlohmann::json::parse(data);
                    std::lock_guard<std::mutex> lock(self->cbMutex_);
                    if (self->messageCallback_) self->messageCallback_(msg);
                } catch (...) {
                    spdlog::debug("SSE: non-JSON data received");
                }
            }
        }
        return size * nmemb;
    }

    std::string sseUrl_;
    std::string postUrl_;
    std::atomic<bool> connected_{false};
    std::mutex cbMutex_;
    std::function<void(const nlohmann::json&)> messageCallback_;
    std::thread sseThread_;
    std::string sseBuffer_;
};

} // namespace closecrab
