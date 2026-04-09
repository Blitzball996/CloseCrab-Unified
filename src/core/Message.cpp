#include "Message.h"
#include "../utils/UUID.h"
#include <chrono>

namespace closecrab {

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

Message Message::makeUser(const std::string& text, const std::string& id) {
    Message m;
    m.type = MessageType::USER;
    m.role = MessageRole::USER;
    m.content.push_back({ContentBlockType::TEXT, text});
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

Message Message::makeAssistant(const std::string& text, const std::string& id) {
    Message m;
    m.type = MessageType::ASSISTANT;
    m.role = MessageRole::ASSISTANT;
    m.content.push_back({ContentBlockType::TEXT, text});
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

Message Message::makeSystem(SystemSubtype subtype, const std::string& text, const std::string& id) {
    Message m;
    m.type = MessageType::SYSTEM;
    m.role = MessageRole::SYSTEM;
    m.systemSubtype = subtype;
    m.content.push_back({ContentBlockType::TEXT, text});
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

Message Message::makeToolUse(const std::string& toolName, const std::string& toolUseId,
                             const nlohmann::json& input, const std::string& id) {
    Message m;
    m.type = MessageType::ASSISTANT;
    m.role = MessageRole::ASSISTANT;
    ContentBlock block;
    block.type = ContentBlockType::TOOL_USE;
    block.toolName = toolName;
    block.toolUseId = toolUseId;
    block.toolInput = input;
    m.content.push_back(std::move(block));
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

Message Message::makeToolResult(const std::string& toolUseId, const nlohmann::json& result,
                                bool isError, const std::string& id) {
    Message m;
    m.type = MessageType::USER;
    m.role = MessageRole::USER;
    m.isToolUseResult = true;
    ContentBlock block;
    block.type = ContentBlockType::TOOL_RESULT;
    block.toolUseId = toolUseId;
    block.toolResult = result;
    block.isError = isError;
    m.content.push_back(std::move(block));
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

// ---- v2 Convenience constructors ----

Message Message::makeInformational(InformationalLevel level, const std::string& text,
                                   const std::string& id) {
    Message m;
    m.type = MessageType::SYSTEM;
    m.role = MessageRole::SYSTEM;
    m.systemSubtype = SystemSubtype::INFORMATIONAL;
    m.infoLevel = level;
    m.isMeta = true;
    std::string prefix;
    switch (level) {
        case InformationalLevel::INFO:       prefix = "[info] "; break;
        case InformationalLevel::WARNING:    prefix = "[warning] "; break;
        case InformationalLevel::ERROR:      prefix = "[error] "; break;
        case InformationalLevel::SUGGESTION: prefix = "[suggestion] "; break;
    }
    m.content.push_back({ContentBlockType::TEXT, prefix + text});
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

Message Message::makeCompactBoundary(const CompactMetadata& meta, const std::string& summary,
                                     const std::string& id) {
    Message m;
    m.type = MessageType::SYSTEM;
    m.role = MessageRole::SYSTEM;
    m.systemSubtype = SystemSubtype::COMPACT_BOUNDARY;
    m.isCompactSummary = true;
    m.compactMeta = meta;
    m.content.push_back({ContentBlockType::TEXT,
        "[Conversation history was compressed. Summary of earlier messages:]\n\n" + summary});
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

Message Message::makeTurnDuration(double durationMs, int tokensUsed, int budgetRemaining,
                                  const std::string& id) {
    Message m;
    m.type = MessageType::SYSTEM;
    m.role = MessageRole::SYSTEM;
    m.systemSubtype = SystemSubtype::TURN_DURATION;
    m.isMeta = true;
    std::string text = "[Turn completed in " + std::to_string(static_cast<int>(durationMs)) + "ms, "
                     + std::to_string(tokensUsed) + " tokens used";
    if (budgetRemaining >= 0) text += ", " + std::to_string(budgetRemaining) + " remaining";
    text += "]";
    m.content.push_back({ContentBlockType::TEXT, text});
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

Message Message::makeApiMetrics(double ttftMs, double outputTokensPerSec,
                                const std::string& id) {
    Message m;
    m.type = MessageType::SYSTEM;
    m.role = MessageRole::SYSTEM;
    m.systemSubtype = SystemSubtype::API_METRICS;
    m.isMeta = true;
    std::string text = "[API metrics: TTFT=" + std::to_string(static_cast<int>(ttftMs)) + "ms"
                     + ", output=" + std::to_string(static_cast<int>(outputTokensPerSec)) + " tok/s]";
    m.content.push_back({ContentBlockType::TEXT, text});
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

Message Message::makeMemorySaved(const std::string& memoryName, const std::string& memoryType,
                                 const std::string& id) {
    Message m;
    m.type = MessageType::SYSTEM;
    m.role = MessageRole::SYSTEM;
    m.systemSubtype = SystemSubtype::MEMORY_SAVED;
    m.isMeta = true;
    m.content.push_back({ContentBlockType::TEXT,
        "[Memory saved: " + memoryName + " (type: " + memoryType + ")]"});
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

Message Message::makeAgentsKilled(const std::vector<std::string>& agentIds,
                                  const std::string& reason, const std::string& id) {
    Message m;
    m.type = MessageType::SYSTEM;
    m.role = MessageRole::SYSTEM;
    m.systemSubtype = SystemSubtype::AGENTS_KILLED;
    m.isMeta = true;
    std::string text = "[Agents terminated (" + reason + "):";
    for (const auto& aid : agentIds) text += " " + aid;
    text += "]";
    m.content.push_back({ContentBlockType::TEXT, text});
    m.uuid = id.empty() ? generateUUID() : id;
    m.timestamp = nowMs();
    return m;
}

std::string Message::getText() const {
    std::string result;
    for (const auto& block : content) {
        if (block.type == ContentBlockType::TEXT) {
            if (!result.empty()) result += "\n";
            result += block.text;
        }
    }
    return result;
}

nlohmann::json Message::toApiJson() const {
    nlohmann::json j;
    j["role"] = (role == MessageRole::USER) ? "user" : "assistant";

    nlohmann::json contentArr = nlohmann::json::array();
    for (const auto& block : content) {
        nlohmann::json b;
        switch (block.type) {
            case ContentBlockType::TEXT:
                b["type"] = "text";
                b["text"] = block.text;
                break;
            case ContentBlockType::TOOL_USE:
                b["type"] = "tool_use";
                b["id"] = block.toolUseId;
                b["name"] = block.toolName;
                b["input"] = block.toolInput;
                break;
            case ContentBlockType::TOOL_RESULT:
                b["type"] = "tool_result";
                b["tool_use_id"] = block.toolUseId;
                b["content"] = block.toolResult.is_string()
                    ? block.toolResult.get<std::string>()
                    : block.toolResult.dump();
                if (block.isError) b["is_error"] = true;
                break;
            case ContentBlockType::THINKING:
                b["type"] = "thinking";
                b["thinking"] = block.text;
                break;
            case ContentBlockType::IMAGE:
                b["type"] = "image";
                b["source"]["type"] = "base64";
                b["source"]["media_type"] = block.mediaType;
                b["source"]["data"] = block.base64Data;
                break;
        }
        contentArr.push_back(std::move(b));
    }
    j["content"] = std::move(contentArr);
    return j;
}

Message Message::fromApiJson(const nlohmann::json& j) {
    Message m;
    m.role = (j.value("role", "") == "assistant") ? MessageRole::ASSISTANT : MessageRole::USER;
    m.type = (m.role == MessageRole::ASSISTANT) ? MessageType::ASSISTANT : MessageType::USER;
    m.uuid = generateUUID();
    m.timestamp = nowMs();

    if (j.contains("content")) {
        if (j["content"].is_string()) {
            m.content.push_back({ContentBlockType::TEXT, j["content"].get<std::string>()});
        } else if (j["content"].is_array()) {
            for (const auto& b : j["content"]) {
                ContentBlock block;
                std::string btype = b.value("type", "text");
                if (btype == "text") {
                    block.type = ContentBlockType::TEXT;
                    block.text = b.value("text", "");
                } else if (btype == "tool_use") {
                    block.type = ContentBlockType::TOOL_USE;
                    block.toolUseId = b.value("id", "");
                    block.toolName = b.value("name", "");
                    block.toolInput = b.value("input", nlohmann::json::object());
                } else if (btype == "tool_result") {
                    block.type = ContentBlockType::TOOL_RESULT;
                    block.toolUseId = b.value("tool_use_id", "");
                    block.toolResult = b.value("content", "");
                    block.isError = b.value("is_error", false);
                } else if (btype == "thinking") {
                    block.type = ContentBlockType::THINKING;
                    block.text = b.value("thinking", "");
                }
                m.content.push_back(std::move(block));
            }
        }
    }

    if (j.contains("stop_reason")) m.stopReason = j["stop_reason"].get<std::string>();
    if (j.contains("usage")) {
        m.usage.inputTokens = j["usage"].value("input_tokens", 0);
        m.usage.outputTokens = j["usage"].value("output_tokens", 0);
        m.usage.cacheReadTokens = j["usage"].value("cache_read_input_tokens", 0);
        m.usage.cacheWriteTokens = j["usage"].value("cache_creation_input_tokens", 0);
    }
    return m;
}

} // namespace closecrab
