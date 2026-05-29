#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <nlohmann/json.hpp>
#include "../core/Message.h"

namespace closecrab {

// Forward declarations
struct AppState;
class PermissionEngine;
class APIClient;
class ToolRegistry;

// ============================================================
// Validation & Permission results
// ============================================================

struct ValidationResult {
    bool valid = true;
    std::vector<std::string> errors;

    static ValidationResult ok() { return {true, {}}; }
    static ValidationResult fail(const std::string& err) { return {false, {err}}; }
};

enum class PermissionResult {
    ALLOWED,
    DENIED,
    ASK_USER
};

// ============================================================
// Tool result
// ============================================================

struct ToolResult {
    bool success = true;
    std::string content;        // Human-readable result text
    nlohmann::json data;        // Structured data (optional)
    std::string error;          // Error message if !success

    std::vector<Message> newMessages;    // Messages to inject into history after this result
    nlohmann::json mcpMeta;              // MCP-specific metadata

    bool hasContextModification = false;
    nlohmann::json contextModification;  // JSON describing context changes

    double elapsedSeconds = 0.0;  // Execution time

    static ToolResult ok(const std::string& content, const nlohmann::json& data = nullptr) {
        return {true, content, data, ""};
    }
    static ToolResult fail(const std::string& error) {
        return {false, "", nullptr, error};
    }
};

// ============================================================
// Tool execution context
// ============================================================

struct ToolContext {
    std::string cwd;
    std::vector<Message>* messages = nullptr;
    AppState* appState = nullptr;
    PermissionEngine* permissionEngine = nullptr;
    std::atomic<bool>* abortFlag = nullptr;

    // API client and tool registry — needed by AgentTool to spawn sub-agents
    APIClient* apiClient = nullptr;
    ToolRegistry* toolRegistry = nullptr;

    // Progress callback: tool can report intermediate progress
    std::function<void(const std::string&)> onProgress;

    // Stream output callback: real-time line-by-line output for execution tools
    std::function<void(const std::string&)> onStreamOutput;

    // Parent's rendered system prompt — AgentTool passes this to sub-agents
    // so they share the prompt cache (claude-code cache-safe fork strategy).
    std::string systemPrompt;

    // Per-session file read state (JackProAi readFileState / FileStateCache pattern,
    // utils/fileStateCache.ts FileState). Tracks what the LLM has read so
    // FileWriteTool/FileEditTool can enforce "read before write" and detect
    // stale writes. JackProAi stores raw `content` for the staleness fallback;
    // we store a hash+size instead (O(1) memory, same correctness for the
    // mtime-false-positive fallback on Windows cloud-sync/antivirus).
    struct ReadState {
        size_t contentHash = 0;     // hash of raw on-disk bytes at read time
        uint64_t contentSize = 0;   // raw file size at read time
        int64_t mtimeMs = 0;        // file mtime (ms since epoch) at read time
        bool hasOffset = false;     // true if a range read (offset set)
        bool hasLimit = false;      // true if a range read (limit set)
        bool isPartialView = false; // true ONLY for auto-injected content differing from disk
                                    // (CLAUDE.md/MEMORY attachments). Normal range Read is NOT partial.
    };
    std::map<std::string, ReadState>* readFileState = nullptr;

    // Session-mutable working directory (JackProAi cwd-tracking: each bash call
    // runs `pwd -P` and writes the result back so `cd subdir` persists to the
    // next call). When set, BashTool reads the post-command cwd and updates it;
    // QueryEngine reuses it as the cwd for the next spawn. Null = no persistence.
    std::string* sessionCwd = nullptr;

    // v2 extended context
    std::map<std::string, std::vector<std::string>> fileHistory;  // file -> list of operations
    std::string attribution;                                       // Who initiated this tool call
    std::vector<std::string> queryChain;                           // Chain of queries leading here
};

// ============================================================
// Tool base class (对标 JackProAi Tool<Input, Output, Progress>)
// ============================================================

class Tool {
public:
    virtual ~Tool() = default;

    // Identity
    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;
    virtual std::vector<std::string> getAliases() const { return {}; }
    virtual std::string getCategory() const { return "general"; }

    // Schema: returns JSON Schema for input validation
    virtual nlohmann::json getInputSchema() const = 0;

    // Lifecycle
    virtual ValidationResult validateInput(const nlohmann::json& input) const;
    virtual PermissionResult checkPermissions(const ToolContext& ctx, const nlohmann::json& input) const;
    virtual ToolResult call(ToolContext& ctx, const nlohmann::json& input) = 0;

    // Result rendering
    virtual std::string renderResult(const ToolResult& result) const;

    // Metadata flags (对标 JackProAi Tool flags)
    virtual bool isReadOnly() const { return false; }
    virtual bool isHidden() const { return false; }
    virtual bool isConcurrencySafe() const { return false; }
    virtual bool isDestructive() const { return false; }
    virtual bool isEnabled() const { return true; }

    // For permission system: human-readable description of what this call does
    virtual std::string getActivityDescription(const nlohmann::json& input) const { return getName(); }

    // For auto-mode classifier: is this a search/read command?
    virtual bool isSearchOrRead(const nlohmann::json& input) const { return isReadOnly(); }

    // Interrupt behavior: "cancel" (abort immediately) or "block" (wait for completion)
    virtual std::string interruptBehavior() const { return "cancel"; }

    // Whether this tool should be deferred (lazy loaded)
    virtual bool shouldDefer() const { return false; }

    // Whether this tool must always be loaded (not deferrable)
    virtual bool alwaysLoad() const { return true; }
};

// Default implementations
inline ValidationResult Tool::validateInput(const nlohmann::json& input) const {
    // Basic schema validation: check required fields from inputSchema
    auto schema = getInputSchema();
    if (schema.contains("required") && schema["required"].is_array()) {
        for (const auto& req : schema["required"]) {
            std::string field = req.get<std::string>();
            if (!input.contains(field) || input[field].is_null()) {
                return ValidationResult::fail("Missing required field: " + field);
            }
        }
    }
    return ValidationResult::ok();
}

inline PermissionResult Tool::checkPermissions(const ToolContext& /*ctx*/, const nlohmann::json& /*input*/) const {
    // Default: read-only tools are always allowed, others need asking
    return isReadOnly() ? PermissionResult::ALLOWED : PermissionResult::ASK_USER;
}

inline std::string Tool::renderResult(const ToolResult& result) const {
    if (result.success) return result.content;
    return "[Error] " + result.error;
}

} // namespace closecrab
