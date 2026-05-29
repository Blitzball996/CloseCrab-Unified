#pragma once

#include "Message.h"
#include "AppState.h"
#include "HistoryCompactor.h"
#include "ContextCollapse.h"
#include "BudgetTracker.h"
#include "ErrorRecovery.h"
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

    TokenBudget tokenBudget;  // Budget configuration

    // Tool filter: if non-empty, only these tools are available (for sub-agents)
    std::vector<std::string> allowedTools;

    // Recursive-spawn guard: sub-agents set this false so they cannot launch
    // their own Agent sub-agents (mirrors JackProAi's fork recursion guard).
    // Prevents unbounded thread explosion that crashed the process.
    bool allowSubagents = true;
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
    // Retry status: surfaced to UI so the user sees "retrying N/M in Xs" instead
    // of a frozen spinner. attempt/maxAttempts are 1-based; reason is short.
    // Kept last so existing positional brace-initializers stay valid.
    std::function<void(int attempt, int maxAttempts, int delayMs, const std::string& reason)> onRetry;
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

    // Manual compaction
    bool compactHistory() { return compactor_.forceCompact(messages_, config_.apiClient); }
    CompactMetadata getLastCompactMetadata() const;

    // Budget management
    void setBudget(const TokenBudget& budget);

    // Serialize/deserialize messages for session persistence
    nlohmann::json serializeMessages() const;
    void deserializeMessages(const nlohmann::json& data);

private:
    std::string buildSystemPrompt() const;
    void processToolUse(const StreamEvent& event, const QueryCallbacks& callbacks);
    ModelConfig buildModelConfig() const;

    QueryEngineConfig config_;
    std::vector<Message> messages_;
    std::string sessionId_;
    std::atomic<bool> interrupted_{false};
    HistoryCompactor compactor_;
    ContextCollapse contextCollapse_;

    // Capped escalation disabled: 8K cap caused repeated truncation of tool
    // calls (model generates text before Write, hits 8K, escalation retries
    // produce different non-tool responses). Just use 64K directly.
    int cappedMaxTokens_ = 64000;
    static constexpr int ESCALATED_MAX_TOKENS = 64000;
    BudgetTracker budgetTracker_;

    // Per-session file read state (§6: write-before-read enforcement)
    std::map<std::string, ToolContext::ReadState> readFileState_;

    // Session-mutable cwd (§10: bash `cd` persistence via pwd -P writeback).
    // Lazily initialized to config_.cwd on first tool call.
    std::string sessionCwd_;

    // JSONL transcript append (Bug C, JackProAi sessionStorage model): index of
    // the next message not yet written to the .jsonl. persistTranscriptDelta()
    // appends messages_[lastPersistedIndex_..] and advances it.
    size_t lastPersistedIndex_ = 0;
    void persistTranscriptDelta();

    // Real token usage from last API response (for accurate pre-flight checks)
    int64_t lastKnownInputTokens_ = 0;
    int lastKnownTokensAtMessageIndex_ = 0;

    // Cached system prompt (rebuilt only when needed)
    mutable std::string cachedSystemPrompt_;
    mutable bool systemPromptDirty_ = true;
};

} // namespace closecrab
