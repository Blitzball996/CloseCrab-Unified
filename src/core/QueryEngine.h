#pragma once

#include "Message.h"
#include "AppState.h"
#include "../api/APIClient.h"
#include "../tools/ToolRegistry.h"
#include "../commands/CommandRegistry.h"
#include "../permissions/PermissionEngine.h"
#include "../memory/MemorySystem.h"
#include <vector>
#include <memory>
#include <functional>
#include <atomic>

namespace closecrab {

// ============================================================
// QueryEngine — core orchestrator (对标 JackProAi QueryEngine)
// ============================================================

struct QueryEngineConfig {
    std::string cwd;
    APIClient* apiClient = nullptr;
    ToolRegistry* toolRegistry = nullptr;
    CommandRegistry* commandRegistry = nullptr;
    PermissionEngine* permissionEngine = nullptr;
    MemorySystem* memorySystem = nullptr;
    AppState* appState = nullptr;

    std::string systemPrompt;
    std::string appendSystemPrompt;
    int maxTurns = 50;
    bool verbose = false;
};

// Callbacks for UI integration
struct QueryCallbacks {
    std::function<void(const std::string&)> onText;
    std::function<void(const std::string&)> onThinking;
    std::function<void(const std::string& toolName, const nlohmann::json& input)> onToolUse;
    std::function<void(const std::string& toolName, const ToolResult& result)> onToolResult;
    std::function<void()> onComplete;
    std::function<void(const std::string&)> onError;
    // Permission prompt: return true to allow, false to deny
    std::function<bool(const std::string& toolName, const std::string& description)> onAskPermission;
};

class QueryEngine {
public:
    explicit QueryEngine(const QueryEngineConfig& config);

    // Submit a user message and process the full turn loop
    void submitMessage(const std::string& prompt, const QueryCallbacks& callbacks);

    // Interrupt current generation
    void interrupt();

    // Message history
    const std::vector<Message>& getMessages() const { return messages_; }
    void clearMessages() { messages_.clear(); }

    // Session
    void setSessionId(const std::string& id) { sessionId_ = id; }
    std::string getSessionId() const { return sessionId_; }

    // Model override
    void setApiClient(APIClient* client) { config_.apiClient = client; }

private:
    std::string buildSystemPrompt() const;
    void processToolUse(const StreamEvent& event, const QueryCallbacks& callbacks);
    ModelConfig buildModelConfig() const;

    QueryEngineConfig config_;
    std::vector<Message> messages_;
    std::string sessionId_;
    std::atomic<bool> interrupted_{false};
};

} // namespace closecrab
