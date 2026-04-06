#pragma once

#include <string>
#include <vector>
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

// System message subtypes
enum class SystemSubtype {
    COMPACT_BOUNDARY,
    API_ERROR,
    LOCAL_COMMAND
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

    // Attachment fields
    AttachmentType attachmentType = AttachmentType::STRUCTURED_OUTPUT;
    nlohmann::json attachmentData;

    // ---- Convenience constructors ----

    static Message makeUser(const std::string& text, const std::string& id = "");
    static Message makeAssistant(const std::string& text, const std::string& id = "");
    static Message makeSystem(SystemSubtype subtype, const std::string& text, const std::string& id = "");
    static Message makeToolUse(const std::string& toolName, const std::string& toolUseId,
                               const nlohmann::json& input, const std::string& id = "");
    static Message makeToolResult(const std::string& toolUseId, const nlohmann::json& result,
                                  bool isError = false, const std::string& id = "");

    // Get plain text content (concatenate all TEXT blocks)
    std::string getText() const;

    // Serialize to/from JSON (for API communication)
    nlohmann::json toApiJson() const;
    static Message fromApiJson(const nlohmann::json& j);
};

} // namespace closecrab
