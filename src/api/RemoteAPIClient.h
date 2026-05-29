#pragma once

#include "APIClient.h"
#include "StreamParser.h"
#include <string>
#include <set>

namespace closecrab {

// Anthropic Claude API client (/v1/messages with SSE streaming)
class RemoteAPIClient : public APIClient {
public:
    RemoteAPIClient(const std::string& apiKey,
                    const std::string& baseUrl = "https://api.anthropic.com",
                    const std::string& model = "claude-opus-4-7");

    void streamChat(
        const std::vector<Message>& messages,
        const std::string& systemPrompt,
        const ModelConfig& config,
        StreamCallback callback
    ) override;

    int countTokens(const std::string& text) const override;
    std::string getModelName() const override { return model_; }
    bool isLocal() const override { return false; }
    bool supportsToolUse() const override { return true; }
    bool supportsThinking() const override { return true; }

    void setFallbackModel(const std::string& model) { fallbackModel_ = model; }

private:
    nlohmann::json buildRequestBody(const std::vector<Message>& messages,
                                     const std::string& systemPrompt,
                                     const ModelConfig& config,
                                     const std::string& modelOverride = "") const;
    void handleSSEEvent(const StreamParser::SSEEvent& event, StreamCallback& callback,
                        std::string& currentToolName, std::string& currentToolId,
                        std::string& currentToolJson) const;

    std::string apiKey_;
    std::string baseUrl_;
    std::string model_;
    std::string fallbackModel_;

    // §3 deterministic microcompact (JackProAi ContentReplacementState.seenIds):
    // once a tool_result is cleared for the context budget, its tool_use_id is
    // recorded here and it stays cleared on EVERY subsequent request with the
    // SAME stub bytes. This keeps the message prefix byte-stable so the
    // server-side prompt cache survives compaction (the old in-place rewrite
    // re-decided each turn → cache miss → the 77K-per-request billing bug).
    // mutable because buildRequestBody is const but must freeze decisions.
    mutable std::set<std::string> clearedToolUseIds_;

    // P1 time-based microcompact (JackProAi timeBasedMCConfig.ts). Tracks when the
    // last request was built; if the gap exceeds the cache TTL the server-side
    // prompt cache has expired anyway, so we proactively clear old tool results
    // before the (inevitably rewritten) request to shrink it. 0 = never sent yet.
    mutable int64_t lastRequestEpochMs_ = 0;
};

} // namespace closecrab
