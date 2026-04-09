#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace closecrab {

// ============================================================
// Content Block Types (对标 JackProAi ContentBlock)
// ============================================================

enum class ContentBlockType {
    TEXT,
    TOOL_USE,
    TOOL_RESULT,
    THINKING,
    IMAGE
};

struct ContentBlock {
    ContentBlockType type = ContentBlockType::TEXT;

    // TEXT / THINKING
    std::string text;

    // TOOL_USE
    std::string toolName;
    std::string toolUseId;
    nlohmann::json toolInput;

    // TOOL_RESULT
    nlohmann::json toolResult;
    bool isError = false;

    // IMAGE
    std::string base64Data;
    std::string mediaType;  // "image/png", "image/jpeg"
};

// ============================================================
// Message Types (对标 JackProAi 6 种消息类型)
// ============================================================

enum class MessageType {
    USER,
    ASSISTANT,
    SYSTEM,
    PROGRESS,
    ATTACHMENT,
    TOMBSTONE
};

enum class MessageRole {
    USER,
    ASSISTANT,
    SYSTEM
};

// Informational level for system messages
enum class InformationalLevel { INFO, WARNING, ERROR, SUGGESTION };

// System message subtypes (16 types, 对标 claude-code-source v2.1.88)
enum class SystemSubtype {
    // Existing
    COMPACT_BOUNDARY,
    MICROCOMPACT_BOUNDARY,
    API_ERROR,
    LOCAL_COMMAND,
    // New (v2 integration)
    INFORMATIONAL,           // info/warning/error/suggestion (uses infoLevel)
    PERMISSION_RETRY,        // Permission retry + command list
    BRIDGE_STATUS,           // Bridge connection status + upgrade hint
    SCHEDULED_TASK_FIRE,     // Cron/scheduled task triggered
    STOP_HOOK_SUMMARY,       // Stop hook execution summary
    TURN_DURATION,           // Turn timing + budget metrics
    AWAY_SUMMARY,            // Away mode summary
    MEMORY_SAVED,            // Memory save notification
    AGENTS_KILLED,           // Agent termination notice
    API_METRICS,             // API perf metrics (TTFT, OTPs)
    THINKING,                // Thinking block marker
    FILE_SNAPSHOT            // File snapshot + content
};

// Attachment types
enum class AttachmentType {
    STRUCTURED_OUTPUT,
    MAX_TURNS_REACHED,
    QUEUED_COMMAND
};

// ============================================================
// Usage tracking
// ============================================================

struct TokenUsage {
    int64_t inputTokens = 0;
    int64_t outputTokens = 0;
    int64_t cacheReadTokens = 0;
    int64_t cacheWriteTokens = 0;
};

// ============================================================
// Compact metadata (tracks compaction details)
// ============================================================

struct CompactMetadata {
    std::string trigger;           // "auto", "reactive", "micro", "collapse", "snip", "manual"
    int preTokens = 0;
    int postTokens = 0;
    int messagesSummarized = 0;
    std::string preservedSegment;
    int64_t timestamp = 0;
    double durationMs = 0.0;
};

// ============================================================
// Message struct
// ============================================================

struct Message {
    MessageType type = MessageType::USER;
    MessageRole role = MessageRole::USER;
    std::vector<ContentBlock> content;
    std::string uuid;
    int64_t timestamp = 0;

    // Stop reason (for assistant messages)
    std::string stopReason;  // "end_turn", "tool_use", "max_tokens"

    // Usage (for assistant messages)
    TokenUsage usage;

    // Flags
    bool isMeta = false;
    bool isToolUseResult = false;
    bool isCompactSummary = false;
    bool isApiErrorMessage = false;

    // System message fields
    SystemSubtype systemSubtype = SystemSubtype::LOCAL_COMMAND;
    InformationalLevel infoLevel = InformationalLevel::INFO;

    // Attachment fields
    AttachmentType attachmentType = AttachmentType::STRUCTURED_OUTPUT;
    nlohmann::json attachmentData;

    // ---- v2 Metadata fields ----
    std::string origin;                              // Where this message came from
    std::string permissionMode;                      // Permission mode when created
    nlohmann::json mcpMeta;                          // MCP-specific metadata
    std::optional<CompactMetadata> compactMeta;      // Compaction metadata

    // ---- Convenience constructors ----

    static Message makeUser(const std::string& text, const std::string& id = "");
    static Message makeAssistant(const std::string& text, const std::string& id = "");
    static Message makeSystem(SystemSubtype subtype, const std::string& text, const std::string& id = "");
    static Message makeToolUse(const std::string& toolName, const std::string& toolUseId,
                               const nlohmann::json& input, const std::string& id = "");
    static Message makeToolResult(const std::string& toolUseId, const nlohmann::json& result,
                                  bool isError = false, const std::string& id = "");

    // ---- v2 Convenience constructors ----

    static Message makeInformational(InformationalLevel level, const std::string& text,
                                     const std::string& id = "");
    static Message makeCompactBoundary(const CompactMetadata& meta, const std::string& summary,
                                       const std::string& id = "");
    static Message makeTurnDuration(double durationMs, int tokensUsed, int budgetRemaining,
                                    const std::string& id = "");
    static Message makeApiMetrics(double ttftMs, double outputTokensPerSec,
                                  const std::string& id = "");
    static Message makeMemorySaved(const std::string& memoryName, const std::string& memoryType,
                                   const std::string& id = "");
    static Message makeAgentsKilled(const std::vector<std::string>& agentIds,
                                    const std::string& reason, const std::string& id = "");

    // Get plain text content (concatenate all TEXT blocks)
    std::string getText() const;

    // Serialize to/from JSON (for API communication)
    nlohmann::json toApiJson() const;
    static Message fromApiJson(const nlohmann::json& j);
};

} // namespace closecrab
