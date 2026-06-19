#include "SessionRouter.h"
#include "../utils/UUID.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace closecrab {

SessionRouter::SessionRouter(const QueryEngineConfig& baseConfig, int maxConcurrent)
    : baseConfig_(baseConfig), maxConcurrent_(maxConcurrent) {
    for (int i = 0; i < maxConcurrent_; ++i) {
        workers_.emplace_back(&SessionRouter::workerLoop, this);
    }
    spdlog::info("[SessionRouter] Started with {} worker threads", maxConcurrent_);
}

SessionRouter::~SessionRouter() {
    shutdown();
}

void SessionRouter::shutdown() {
    shutdown_ = true;
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

std::string SessionRouter::registerClient(const std::string& username, const std::string& cwd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto clientId = generateUUID();
    auto session = std::make_unique<ClientSession>();
    session->clientId = clientId;
    session->username = username;
    session->workingDirectory = cwd.empty() ? baseConfig_.cwd : cwd;
    session->lastActive = std::chrono::steady_clock::now();
    sessions_[clientId] = std::move(session);
    spdlog::info("[SessionRouter] Client registered: {} ({})", username, clientId);
    return clientId;
}

void SessionRouter::disconnectClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(clientId);
    if (it != sessions_.end()) {
        it->second->connected = false;
        it->second->interrupted = true;
        spdlog::info("[SessionRouter] Client disconnected: {}", clientId);
    }
}

ClientSession* SessionRouter::getSession(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(clientId);
    return it != sessions_.end() ? it->second.get() : nullptr;
}

std::vector<std::string> SessionRouter::listClientIds() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    for (auto& [id, session] : sessions_) {
        if (session->connected) ids.push_back(id);
    }
    return ids;
}

int SessionRouter::clientCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (auto& [_, s] : sessions_) {
        if (s->connected) ++count;
    }
    return count;
}

void SessionRouter::submitRequest(const std::string& clientId, const std::string& message, int priority) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(clientId);
        if (it == sessions_.end() || !it->second->connected) {
            spdlog::warn("[SessionRouter] submitRequest for unknown/disconnected client: {}", clientId);
            return;
        }
        it->second->lastActive = std::chrono::steady_clock::now();
        requestQueue_.push_back({clientId, message, priority, std::chrono::steady_clock::now()});

        if (priority > 0) {
            std::stable_sort(requestQueue_.begin(), requestQueue_.end(),
                [](const PendingRequest& a, const PendingRequest& b) {
                    return a.priority > b.priority;
                });
        }
    }
    cv_.notify_one();

    if (clientCallback_) {
        nlohmann::json j; j["position"] = (int)requestQueue_.size();
        clientCallback_(clientId, "queued", j);
    }
}

void SessionRouter::abortClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(clientId);
    if (it != sessions_.end()) {
        it->second->interrupted = true;
    }
}

void SessionRouter::setClientCallback(ClientCallback cb) {
    clientCallback_ = std::move(cb);
}

void SessionRouter::workerLoop() {
    while (!shutdown_) {
        PendingRequest req;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return shutdown_ || !requestQueue_.empty(); });
            if (shutdown_ && requestQueue_.empty()) return;
            if (requestQueue_.empty()) continue;
            req = std::move(requestQueue_.front());
            requestQueue_.pop_front();
        }
        activeWorkers_++;
        // A worker thread must NEVER let an exception escape: an uncaught throw
        // here unwinds out of the thread function → std::terminate → process
        // crash (the same 0xE06D7363 class as the curl-callback bug, but on the
        // mobile/web remote-control path). submitMessage() can throw on 503/504,
        // bad UTF-8, JSON errors, etc. Catch everything, report it to the client,
        // and keep the worker alive for the next request.
        try {
            processRequest(req);
        } catch (const std::exception& e) {
            spdlog::error("[SessionRouter] worker caught exception: {}", e.what());
            try {
                if (clientCallback_) {
                    nlohmann::json j; j["error"] = std::string("worker error: ") + e.what();
                    clientCallback_(req.clientId, "error", j);
                }
            } catch (...) { /* never let the error-report itself escape */ }
        } catch (...) {
            spdlog::error("[SessionRouter] worker caught unknown exception");
            try {
                if (clientCallback_) {
                    nlohmann::json j; j["error"] = "worker error: unknown exception";
                    clientCallback_(req.clientId, "error", j);
                }
            } catch (...) {}
        }
        activeWorkers_--;
    }
}

void SessionRouter::processRequest(const PendingRequest& req) {
    ClientSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(req.clientId);
        if (it == sessions_.end() || !it->second->connected) return;
        session = it->second.get();
    }

    session->isGenerating = true;
    session->interrupted = false;
    // Ensure the "generating" flag is cleared no matter how we leave this
    // function — including if submitMessage() throws. Without this, an exception
    // would leave the client stuck showing "generating" forever (the success
    // path's reset at the bottom would be skipped).
    struct GenGuard {
        ClientSession* s;
        ~GenGuard() { if (s) s->isGenerating = false; }
    } genGuard{session};

    if (clientCallback_) {
        nlohmann::json j; j["message"] = req.message;
        clientCallback_(req.clientId, "generating", j);
    }

    auto callbacks = buildCallbacksForClient(req.clientId);

    QueryEngine engine(baseConfig_);
    engine.deserializeMessages(nlohmann::json::array());

    if (!session->messages.empty()) {
        auto serialized = engine.serializeMessages();
        // Rebuild from session messages by re-submitting via deserialize
        // For now, start fresh each time (session history will be added incrementally)
    }

    engine.submitMessage(req.message, callbacks);

    auto& engineMsgs = engine.getMessages();
    session->messages.assign(engineMsgs.begin(), engineMsgs.end());

    session->isGenerating = false;

    if (clientCallback_) {
        clientCallback_(req.clientId, "complete", nlohmann::json::object());
    }
}

QueryCallbacks SessionRouter::buildCallbacksForClient(const std::string& clientId) {
    QueryCallbacks cb;
    cb.onText = [this, clientId](const std::string& text) {
        if (clientCallback_) {
            nlohmann::json j; j["data"] = text;
            clientCallback_(clientId, "text", j);
        }
    };
    cb.onThinking = [this, clientId](const std::string& text) {
        if (clientCallback_) {
            nlohmann::json j; j["data"] = text;
            clientCallback_(clientId, "thinking", j);
        }
    };
    cb.onToolUse = [this, clientId](const std::string& name, const nlohmann::json& input) {
        if (clientCallback_) {
            nlohmann::json j; j["name"] = name; j["input"] = input;
            clientCallback_(clientId, "tool_use", j);
        }
    };
    cb.onToolResult = [this, clientId](const std::string& name, const ToolResult& result) {
        if (clientCallback_) {
            nlohmann::json j;
            j["name"] = name;
            j["content"] = result.content;
            j["success"] = result.success;
            clientCallback_(clientId, "tool_result", j);
        }
    };
    cb.onComplete = [this, clientId]() {
        if (clientCallback_) {
            clientCallback_(clientId, "turn_complete", nlohmann::json::object());
        }
    };
    cb.onError = [this, clientId](const std::string& err) {
        if (clientCallback_) {
            nlohmann::json j; j["message"] = err;
            clientCallback_(clientId, "error", j);
        }
    };
    cb.onAskPermission = [](const std::string&, const std::string&) -> bool {
        return true;
    };
    return cb;
}

} // namespace closecrab
