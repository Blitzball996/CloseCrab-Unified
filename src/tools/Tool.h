#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <nlohmann/json.hpp>

namespace closecrab {

// Forward declarations
struct Message;
struct AppState;
class PermissionEngine;

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

    // Progress callback: tool can report intermediate progress
    std::function<void(const std::string&)> onProgress;

    // File read cache (mtime-based dedup, like JackProAi readFileState)
    std::map<std::string, int64_t>* fileReadCache = nullptr;
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
