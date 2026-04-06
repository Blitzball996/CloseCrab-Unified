#include "QueryEngine.h"
#include "../utils/UUID.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace closecrab {

QueryEngine::QueryEngine(const QueryEngineConfig& config) : config_(config) {}

std::string QueryEngine::buildSystemPrompt() const {
    std::string prompt = config_.systemPrompt;

    // Append CLAUDE.md content if available
    if (config_.appState && !config_.appState->claudeMdContent.empty()) {
        prompt += "\n\n" + config_.appState->claudeMdContent;
    }

    // Only append tool descriptions for local models (remote APIs use native tool_use)
    // But keep it short — the system prompt already lists tools
    // if (config_.toolRegistry) { ... }

    // Append user-specified extra prompt
    if (!config_.appendSystemPrompt.empty()) {
        prompt += "\n\n" + config_.appendSystemPrompt;
    }

    return prompt;
}

ModelConfig QueryEngine::buildModelConfig() const {
    ModelConfig mc;
    mc.maxTokens = 4096;
    mc.temperature = 0.7f;
    mc.stream = true;

    if (config_.appState) {
        mc.thinkingEnabled = config_.appState->thinkingConfig.enabled;
        mc.thinkingBudgetTokens = config_.appState->thinkingConfig.budgetTokens;
    }

    // Send only core tool definitions to API (keep request small for proxy compatibility)
    if (config_.toolRegistry && config_.apiClient && !config_.apiClient->isLocal()) {
        nlohmann::json coreTools = nlohmann::json::array();
        static const std::vector<std::string> coreToolNames = {
            "Read", "Write", "Edit", "Glob", "Grep", "Bash"
        };
        for (const auto& name : coreToolNames) {
            Tool* t = config_.toolRegistry->getTool(name);
            if (t && t->isEnabled()) {
                nlohmann::json def;
                def["name"] = t->getName();
                def["description"] = t->getDescription();
                def["input_schema"] = t->getInputSchema();
                coreTools.push_back(std::move(def));
            }
        }
        mc.tools = std::move(coreTools);
    }

    return mc;
}

void QueryEngine::processToolUse(const StreamEvent& event, const QueryCallbacks& callbacks) {
    if (!config_.toolRegistry) return;

    Tool* tool = config_.toolRegistry->getTool(event.toolName);
    if (!tool) {
        spdlog::warn("Unknown tool: {}", event.toolName);
        auto errResult = ToolResult::fail("Unknown tool: " + event.toolName);
        messages_.push_back(Message::makeToolResult(event.toolUseId,
            nlohmann::json(errResult.error), true));
        return;
    }

    // Notify UI
    if (callbacks.onToolUse) callbacks.onToolUse(event.toolName, event.toolInput);

    // Validate input
    auto validation = tool->validateInput(event.toolInput);
    if (!validation.valid) {
        std::string errMsg = "Validation failed: ";
        for (const auto& e : validation.errors) errMsg += e + "; ";
        auto result = ToolResult::fail(errMsg);
        if (callbacks.onToolResult) callbacks.onToolResult(event.toolName, result);
        messages_.push_back(Message::makeToolResult(event.toolUseId,
            nlohmann::json(errMsg), true));
        return;
    }

    // Check permissions
    PermissionDecision decision = PermissionDecision::ALLOWED;
    if (config_.permissionEngine) {
        std::string action = event.toolName + " " + event.toolInput.dump();
        decision = config_.permissionEngine->check(
            event.toolName, action, tool->isReadOnly(), tool->isDestructive());
        config_.permissionEngine->logDecision(event.toolName, action, decision);
    }

    if (decision == PermissionDecision::ASK_USER && callbacks.onAskPermission) {
        std::string desc = tool->getActivityDescription(event.toolInput);
        bool allowed = callbacks.onAskPermission(event.toolName, desc);
        decision = allowed ? PermissionDecision::ALLOWED : PermissionDecision::DENIED;
    }

    if (decision == PermissionDecision::DENIED) {
        auto result = ToolResult::fail("Permission denied for " + event.toolName);
        if (callbacks.onToolResult) callbacks.onToolResult(event.toolName, result);
        messages_.push_back(Message::makeToolResult(event.toolUseId,
            nlohmann::json(result.error), true));
        return;
    }

    // Execute tool
    ToolContext ctx;
    ctx.cwd = config_.cwd;
    ctx.messages = &messages_;
    ctx.appState = config_.appState;
    ctx.permissionEngine = config_.permissionEngine;
    ctx.abortFlag = &interrupted_;

    auto start = std::chrono::steady_clock::now();
    ToolResult result = tool->call(ctx, event.toolInput);
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (config_.appState) {
        config_.appState->totalToolDuration.store(
            config_.appState->totalToolDuration.load() + elapsed);
    }

    if (callbacks.onToolResult) callbacks.onToolResult(event.toolName, result);

    // Add tool result to messages
    nlohmann::json resultJson = result.success ? nlohmann::json(result.content) : nlohmann::json(result.error);
    messages_.push_back(Message::makeToolResult(event.toolUseId, resultJson, !result.success));
}

void QueryEngine::submitMessage(const std::string& prompt, const QueryCallbacks& callbacks) {
    interrupted_ = false;

    // Add user message
    messages_.push_back(Message::makeUser(prompt));

    // Save to memory
    if (config_.memorySystem && !sessionId_.empty()) {
        config_.memorySystem->addMemory(sessionId_, "user", prompt);
    }

    std::string systemPrompt = buildSystemPrompt();
    ModelConfig modelConfig = buildModelConfig();
    int turnCount = 0;

    // Multi-turn loop: keep going while LLM requests tool use
    while (turnCount < config_.maxTurns && !interrupted_) {
        turnCount++;
        std::string accumulatedText;
        std::vector<StreamEvent> pendingToolCalls;
        bool gotStop = false;
        std::string stopReason;

        // Call API
        auto start = std::chrono::steady_clock::now();

        config_.apiClient->streamChat(messages_, systemPrompt, modelConfig,
            [&](const StreamEvent& event) {
                if (interrupted_) return;

                switch (event.type) {
                    case StreamEvent::EVT_TEXT:
                        accumulatedText += event.content;
                        if (callbacks.onText) callbacks.onText(event.content);
                        break;

                    case StreamEvent::EVT_THINKING:
                        if (callbacks.onThinking) callbacks.onThinking(event.content);
                        break;

                    case StreamEvent::EVT_TOOL_USE:
                        pendingToolCalls.push_back(event);
                        break;

                    case StreamEvent::EVT_STOP:
                        gotStop = true;
                        stopReason = event.stopReason;
                        // Track usage
                        if (config_.appState && event.usage.outputTokens > 0) {
                            config_.appState->trackUsage(
                                config_.apiClient->getModelName(),
                                event.usage.inputTokens,
                                event.usage.outputTokens);
                        }
                        break;

                    case StreamEvent::EVT_USAGE_UPDATE:
                        if (config_.appState) {
                            config_.appState->trackUsage(
                                config_.apiClient->getModelName(),
                                event.usage.inputTokens,
                                event.usage.outputTokens);
                        }
                        break;

                    case StreamEvent::EVT_ERROR:
                        if (callbacks.onError) callbacks.onError(event.content);
                        break;
                }
            }
        );

        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (config_.appState) {
            config_.appState->totalAPIDuration.store(
                config_.appState->totalAPIDuration.load() + elapsed);
        }

        // Add assistant message to history
        if (!accumulatedText.empty() || !pendingToolCalls.empty()) {
            Message assistantMsg;
            assistantMsg.type = MessageType::ASSISTANT;
            assistantMsg.role = MessageRole::ASSISTANT;
            assistantMsg.uuid = generateUUID();
            assistantMsg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            assistantMsg.stopReason = stopReason;

            if (!accumulatedText.empty()) {
                assistantMsg.content.push_back({ContentBlockType::TEXT, accumulatedText});
            }
            for (const auto& tc : pendingToolCalls) {
                ContentBlock block;
                block.type = ContentBlockType::TOOL_USE;
                block.toolName = tc.toolName;
                block.toolUseId = tc.toolUseId;
                block.toolInput = tc.toolInput;
                assistantMsg.content.push_back(std::move(block));
            }
            messages_.push_back(std::move(assistantMsg));
        }

        // Save assistant response to memory
        if (config_.memorySystem && !sessionId_.empty() && !accumulatedText.empty()) {
            config_.memorySystem->addMemory(sessionId_, "assistant", accumulatedText);
        }

        // Process tool calls
        if (!pendingToolCalls.empty() && !interrupted_) {
            for (const auto& tc : pendingToolCalls) {
                if (interrupted_) break;
                processToolUse(tc, callbacks);
            }

            // For local models: tool executed successfully, do ONE more turn
            // to let the model summarize, then stop regardless.
            // Remote APIs (Claude/OpenAI) handle multi-turn naturally.
            if (config_.apiClient && config_.apiClient->isLocal()) {
                // One more turn for summary
                turnCount++;
                if (turnCount >= config_.maxTurns) break;

                std::string summaryText;
                config_.apiClient->streamChat(messages_, systemPrompt, modelConfig,
                    [&](const StreamEvent& event) {
                        if (interrupted_) return;
                        if (event.type == StreamEvent::EVT_TEXT) {
                            // Filter special tokens
                            if (event.content.find("<|im_") == std::string::npos &&
                                event.content.find("<|endoftext|>") == std::string::npos) {
                                summaryText += event.content;
                                if (callbacks.onText) callbacks.onText(event.content);
                            }
                        }
                    }
                );

                if (!summaryText.empty()) {
                    messages_.push_back(Message::makeAssistant(summaryText));
                    if (config_.memorySystem && !sessionId_.empty()) {
                        config_.memorySystem->addMemory(sessionId_, "assistant", summaryText);
                    }
                }
                break; // Done — don't loop again
            }

            // Remote API: continue the loop naturally
            continue;
        }

        // No tool calls — we're done
        break;
    }

    if (callbacks.onComplete) callbacks.onComplete();
}

void QueryEngine::interrupt() {
    interrupted_ = true;
}

} // namespace closecrab
