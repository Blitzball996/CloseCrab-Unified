#pragma once

#include "QueryEngine.h"
#include "Message.h"
#include <string>
#include <map>
#include <deque>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace closecrab {

struct PendingRequest {
    std::string clientId;
    std::string message;
    int priority = 0;
    std::chrono::steady_clock::time_point queuedAt;
};

struct ClientSession {
    std::string clientId;
    std::string username;
    std::string workingDirectory;
    std::vector<Message> messages;
    std::chrono::steady_clock::time_point lastActive;
    std::atomic<bool> connected{true};
    std::atomic<bool> isGenerating{false};
    std::atomic<bool> interrupted{false};
    int sequenceSlot = -1;
};

class SessionRouter {
public:
    using ClientCallback = std::function<void(const std::string& clientId,
                                              const std::string& event,
                                              const nlohmann::json& data)>;

    SessionRouter(const QueryEngineConfig& baseConfig, int maxConcurrent = 4);
    ~SessionRouter();

    std::string registerClient(const std::string& username, const std::string& cwd = "");
    void disconnectClient(const std::string& clientId);
    ClientSession* getSession(const std::string& clientId);
    std::vector<std::string> listClientIds();
    int clientCount() const;

    void submitRequest(const std::string& clientId, const std::string& message, int priority = 0);
    void abortClient(const std::string& clientId);

    void setClientCallback(ClientCallback cb);

    void shutdown();

private:
    void workerLoop();
    void processRequest(const PendingRequest& req);
    QueryCallbacks buildCallbacksForClient(const std::string& clientId);

    QueryEngineConfig baseConfig_;
    int maxConcurrent_;
    std::map<std::string, std::unique_ptr<ClientSession>> sessions_;
    std::deque<PendingRequest> requestQueue_;

    std::vector<std::thread> workers_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_{false};
    std::atomic<int> activeWorkers_{0};

    ClientCallback clientCallback_;
};

} // namespace closecrab
