#include "QueryEngine.h"
#include "../utils/UUID.h"
#include "../utils/StringUtils.h"
#include "../api/APIError.h"
#include "../hooks/HookManager.h"
#include "../memory/FileMemoryManager.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <future>
#include <mutex>
#include <regex>
#include <set>

namespace closecrab {

QueryEngine::QueryEngine(const QueryEngineConfig& config) : config_(config) {}

std::string QueryEngine::buildSystemPrompt() const {
    if (!systemPromptDirty_ && !cachedSystemPrompt_.empty()) {
        return cachedSystemPrompt_;
    }

    std::string prompt = config_.systemPrompt;

    // Append CLAUDE.md content if available
    if (config_.appState && !config_.appState->claudeMdContent.empty()) {
        prompt += "\n\n" + config_.appState->claudeMdContent;
    }

    // For local models: inject tool descriptions into system prompt
    // (Remote APIs use native tool_use, but local models need text-based tool info)
    if (config_.toolRegistry && config_.apiClient && config_.apiClient->isLocal()) {
        prompt += "\n\n# Available Tools\n"
                  "When you need to use a tool, respond with:\n"
                  "SKILL: <tool_name>\nPARAMS: <json_params>\n\n";

        bool planMode = config_.appState && config_.appState->planMode;
        for (Tool* t : config_.toolRegistry->getAllTools()) {
            if (!t->isEnabled() || t->isHidden()) continue;
            if (planMode && !t->isReadOnly()) continue;
            prompt += "- " + t->getName() + ": " + t->getDescription() + "\n";
        }
    }

    // For remote APIs: list ALL tools in system prompt so LLM knows about them
    // (Only core tools are sent via API tools field due to proxy size limits)
    // Tools not in the API tools field can be invoked via text format: SKILL: name\nPARAMS: {...}
    if (config_.toolRegistry && config_.apiClient && !config_.apiClient->isLocal()) {
        prompt += "\n\n# Additional Tools\n"
                  "Beyond the tools available via tool_use, you also have these tools.\n"
                  "To use them, output in your text response:\nSKILL: <tool_name>\nPARAMS: <json>\n\n";

        bool planMode = config_.appState && config_.appState->planMode;
        // Only list tools NOT in the API tools field (tier1+tier2 are already in tools field)
        static const std::set<std::string> apiTools = {
            "Read", "Write", "Edit", "Glob", "Grep", "Bash",
            "AskUserQuestion", "Agent", "WebSearch", "WebFetch", "TaskCreate", "TaskList"
        };
        for (Tool* t : config_.toolRegistry->getAllTools()) {
            if (!t->isEnabled() || t->isHidden()) continue;
            if (planMode && !t->isReadOnly()) continue;
            if (apiTools.count(t->getName())) continue; // Already in API tools
            prompt += "- " + t->getName() + ": " + t->getDescription() + "\n";
        }
    }

    // Append file-based memories (MEMORY.md index) — cache to avoid disk IO every turn
    static std::string cachedMemIndex;
    static bool memIndexLoaded = false;
    if (!memIndexLoaded) {
        FileMemoryManager memMgr(config_.cwd);
        cachedMemIndex = memMgr.loadIndex();
        memIndexLoaded = true;
    }
    if (!cachedMemIndex.empty()) {
        prompt += "\n\n# Memories\n" + cachedMemIndex;
    }

    // Project context: working directory, git branch
    prompt += "\n\n# Environment\n";
    prompt += "- Working directory: " + config_.cwd + "\n";
    if (config_.appState) {
        prompt += "- Model: " + config_.appState->currentModel + "\n";
        if (config_.appState->planMode) prompt += "- Mode: PLAN (read-only)\n";
    }

    // Append user-specified extra prompt
    if (!config_.appendSystemPrompt.empty()) {
        prompt += "\n\n" + config_.appendSystemPrompt;
    }

    cachedSystemPrompt_ = prompt;
    systemPromptDirty_ = false;
    return prompt;
}

ModelConfig QueryEngine::buildModelConfig() const {
    static ModelConfig cachedMc;
    static bool mcCached = false;
    static bool lastPlanMode = false;

    // Invalidate cache if plan mode changed
    bool currentPlanMode = config_.appState && config_.appState->planMode;
    if (mcCached && lastPlanMode == currentPlanMode) return cachedMc;

    ModelConfig mc;
    mc.maxTokens = 4096;
    mc.temperature = 0.7f;
    mc.stream = true;

    if (config_.appState) {
        mc.thinkingEnabled = config_.appState->thinkingConfig.enabled;
        mc.thinkingBudgetTokens = config_.appState->thinkingConfig.budgetTokens;
    }

    // Send tool definitions to API (remote APIs use native tool_use)
    if (config_.toolRegistry && config_.apiClient && !config_.apiClient->isLocal()) {
        nlohmann::json toolDefs = nlohmann::json::array();

        if (!config_.allowedTools.empty()) {
            // Sub-agent: only send allowed tools
            for (const auto& name : config_.allowedTools) {
                Tool* t = config_.toolRegistry->getTool(name);
                if (t && t->isEnabled()) {
                    nlohmann::json def;
                    def["name"] = t->getName();
                    def["description"] = t->getDescription();
                    def["input_schema"] = t->getInputSchema();
                    toolDefs.push_back(std::move(def));
                }
            }
        } else {
            // Main engine: send tools in priority tiers
            // Proxy/relay services have request size limits — send core tools first
            // All 42 tools are registered and executable, but we only tell the LLM
            // about the most important ones to keep request size manageable.
            // Use /tools command to see all available tools.
            static const std::vector<std::string> tier1 = {
                "Read", "Write", "Edit", "Glob", "Grep", "Bash",
                "AskUserQuestion", "Agent"
            };
            static const std::vector<std::string> tier2 = {
                "WebSearch", "WebFetch", "TaskCreate", "TaskList"
            };

            bool planMode = config_.appState && config_.appState->planMode;

            auto addTool = [&](const std::string& name) {
                Tool* t = config_.toolRegistry->getTool(name);
                if (!t || !t->isEnabled() || t->isHidden()) return;
                if (planMode && !t->isReadOnly()) return;
                nlohmann::json def;
                def["name"] = t->getName();
                // Ultra-compact: short desc, minimal schema (no property descriptions)
                std::string desc = t->getDescription();
                auto dot = desc.find('.');
                if (dot != std::string::npos && dot < 60) desc = desc.substr(0, dot);
                else if (desc.size() > 50) desc = desc.substr(0, 50);
                def["description"] = desc;

                nlohmann::json schema = t->getInputSchema();
                if (schema.contains("properties") && schema["properties"].is_object()) {
                    nlohmann::json minProps;
                    for (auto& [key, val] : schema["properties"].items()) {
                        minProps[key] = {{"type", val.value("type", "string")}};
                    }
                    schema["properties"] = std::move(minProps);
                }
                def["input_schema"] = schema;
                toolDefs.push_back(std::move(def));
            };

            for (const auto& name : tier1) addTool(name);
            for (const auto& name : tier2) addTool(name);
        }

        mc.tools = std::move(toolDefs);
    }

    cachedMc = mc;
    mcCached = true;
    lastPlanMode = currentPlanMode;
    return mc;
}

void QueryEngine::processToolUse(const StreamEvent& event, const QueryCallbacks& callbacks) {
    if (!config_.toolRegistry) return;

    // Check tool filter (for sub-agents with restricted tool sets)
    if (!config_.allowedTools.empty()) {
        bool allowed = false;
        for (const auto& name : config_.allowedTools) {
            if (name == event.toolName) { allowed = true; break; }
        }
        if (!allowed) {
            auto errResult = ToolResult::fail("Tool not available in this agent: " + event.toolName);
            messages_.push_back(Message::makeToolResult(event.toolUseId,
                nlohmann::json(errResult.error), true));
            return;
        }
    }

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

    // Fire PreToolUse hooks
    auto& hookMgr = HookManager::getInstance();
    if (hookMgr.hasHooks()) {
        auto hookResult = hookMgr.fire(HookEvent::PRE_TOOL_USE, event.toolName, event.toolInput);
        if (hookResult.blocked) {
            auto result = ToolResult::fail("Blocked by hook: " + hookResult.error);
            if (callbacks.onToolResult) callbacks.onToolResult(event.toolName, result);
            messages_.push_back(Message::makeToolResult(event.toolUseId,
                nlohmann::json(result.error), true));
            return;
        }
    }

    // Execute tool
    ToolContext ctx;
    ctx.cwd = config_.cwd;
    ctx.messages = &messages_;
    ctx.appState = config_.appState;
    ctx.permissionEngine = config_.permissionEngine;
    ctx.abortFlag = &interrupted_;
    ctx.apiClient = config_.apiClient;
    ctx.toolRegistry = config_.toolRegistry;

    auto start = std::chrono::steady_clock::now();
    ToolResult result;
    try {
        result = tool->call(ctx, event.toolInput);
    } catch (const std::exception& e) {
        spdlog::error("Tool {} threw exception: {}", event.toolName, e.what());
        result = ToolResult::fail(std::string("Internal error: ") + e.what());
    } catch (...) {
        spdlog::error("Tool {} threw unknown exception", event.toolName);
        result = ToolResult::fail("Internal error (unknown exception)");
    }
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (config_.appState) {
        config_.appState->totalToolDuration.store(
            config_.appState->totalToolDuration.load() + elapsed);
    }

    if (callbacks.onToolResult) callbacks.onToolResult(event.toolName, result);

    // Fire PostToolUse hooks
    if (hookMgr.hasHooks()) {
        hookMgr.fire(HookEvent::POST_TOOL_USE, event.toolName, event.toolInput);
    }

    // Add tool result to messages (ensure valid UTF-8 for JSON serialization)
    std::string safeContent = ensureUtf8(result.success ? result.content : result.error);
    nlohmann::json resultJson = nlohmann::json(safeContent);
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

        // Auto-compact history if approaching context limit (check every 5 turns)
        if (turnCount % 5 == 0) {
            compactor_.compactIfNeeded(messages_, config_.apiClient);
        }

        std::string accumulatedText;
        std::vector<StreamEvent> pendingToolCalls;
        bool gotStop = false;
        std::string stopReason;

        // Call API (with error handling for retryable failures)
        auto start = std::chrono::steady_clock::now();
        bool apiCallFailed = false;

        try {
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
        } catch (const APIError& e) {
            spdlog::error("API call failed: {}", e.what());
            if (callbacks.onError) callbacks.onError(e.what());
            apiCallFailed = true;
        } catch (const std::exception& e) {
            spdlog::error("Unexpected error during API call: {}", e.what());
            if (callbacks.onError) callbacks.onError(std::string("Unexpected error: ") + e.what());
            apiCallFailed = true;
        }

        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (config_.appState) {
            config_.appState->totalAPIDuration.store(
                config_.appState->totalAPIDuration.load() + elapsed);
        }

        if (apiCallFailed) break; // Don't continue the turn loop on API failure

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

        // Process tool calls (parallel when multiple, sequential when single)
        if (!pendingToolCalls.empty() && !interrupted_) {
            if (pendingToolCalls.size() == 1) {
                // Single tool: execute directly
                processToolUse(pendingToolCalls[0], callbacks);
            } else {
                // Multiple tools: execute in parallel using std::async
                std::vector<std::future<void>> futures;
                std::mutex messagesMutex;

                for (const auto& tc : pendingToolCalls) {
                    if (interrupted_) break;
                    futures.push_back(std::async(std::launch::async, [&, tc]() {
                        if (interrupted_) return;
                        // processToolUse writes to messages_ — need synchronization
                        // Create a local copy of the event to avoid race
                        StreamEvent localEvent = tc;

                        // Execute tool (callbacks are thread-safe for output)
                        if (!config_.toolRegistry) return;
                        Tool* tool = config_.toolRegistry->getTool(localEvent.toolName);
                        if (!tool) {
                            std::lock_guard<std::mutex> lock(messagesMutex);
                            messages_.push_back(Message::makeToolResult(localEvent.toolUseId,
                                nlohmann::json("Unknown tool: " + localEvent.toolName), true));
                            return;
                        }

                        if (callbacks.onToolUse) callbacks.onToolUse(localEvent.toolName, localEvent.toolInput);

                        ToolContext ctx;
                        ctx.cwd = config_.cwd;
                        ctx.messages = &messages_;
                        ctx.appState = config_.appState;
                        ctx.permissionEngine = config_.permissionEngine;
                        ctx.abortFlag = &interrupted_;
                        ctx.apiClient = config_.apiClient;
                        ctx.toolRegistry = config_.toolRegistry;

                        ToolResult result = tool->call(ctx, localEvent.toolInput);
                        if (callbacks.onToolResult) callbacks.onToolResult(localEvent.toolName, result);

                        std::string safeContent = ensureUtf8(result.success ? result.content : result.error);
                        nlohmann::json resultJson = nlohmann::json(safeContent);
                        std::lock_guard<std::mutex> lock(messagesMutex);
                        messages_.push_back(Message::makeToolResult(
                            localEvent.toolUseId, resultJson, !result.success));
                    }));
                }

                // Wait for all to complete
                for (auto& f : futures) {
                    if (f.valid()) f.wait();
                }
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

        // No native tool calls — check for text-based SKILL: format in accumulated text
        // This handles tools listed in system prompt but not in API tools field
        if (!accumulatedText.empty() && accumulatedText.find("SKILL:") != std::string::npos) {
            std::string toolName, toolUseId;
            nlohmann::json toolInput;
            // Reuse LocalLLMClient's parsing logic
            std::regex skillRegex(R"(SKILL:\s*(\w+)\s*\nPARAMS:\s*(\{[\s\S]*?\}))");
            std::smatch match;
            if (std::regex_search(accumulatedText, match, skillRegex)) {
                toolName = match[1].str();
                toolUseId = "text_" + std::to_string(turnCount);
                try {
                    toolInput = nlohmann::json::parse(match[2].str());
                } catch (...) {
                    toolInput = nlohmann::json::object();
                }

                // Create a synthetic tool_use event and process it
                StreamEvent toolEvent;
                toolEvent.type = StreamEvent::EVT_TOOL_USE;
                toolEvent.toolName = toolName;
                toolEvent.toolUseId = toolUseId;
                toolEvent.toolInput = toolInput;
                processToolUse(toolEvent, callbacks);

                // Continue the loop to let LLM see the tool result
                continue;
            }
        }

        // No tool calls — we're done
        break;
    }

    if (callbacks.onComplete) callbacks.onComplete();
}

void QueryEngine::interrupt() {
    interrupted_ = true;
}

nlohmann::json QueryEngine::serializeMessages() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& msg : messages_) {
        nlohmann::json m;
        m["role"] = (msg.role == MessageRole::USER) ? "user" :
                    (msg.role == MessageRole::ASSISTANT) ? "assistant" : "system";
        m["text"] = msg.getText();
        m["timestamp"] = msg.timestamp;
        // Serialize tool use blocks
        nlohmann::json blocks = nlohmann::json::array();
        for (const auto& block : msg.content) {
            nlohmann::json b;
            b["type"] = static_cast<int>(block.type);
            if (block.type == ContentBlockType::TEXT) b["text"] = block.text;
            else if (block.type == ContentBlockType::TOOL_USE) {
                b["tool_name"] = block.toolName;
                b["tool_id"] = block.toolUseId;
                b["tool_input"] = block.toolInput;
            }
            blocks.push_back(std::move(b));
        }
        m["content"] = std::move(blocks);
        arr.push_back(std::move(m));
    }
    return arr;
}

void QueryEngine::deserializeMessages(const nlohmann::json& data) {
    messages_.clear();
    if (!data.is_array()) return;

    for (const auto& m : data) {
        std::string role = m.value("role", "user");
        std::string text = m.value("text", "");
        if (text.empty()) continue;

        if (role == "user") {
            messages_.push_back(Message::makeUser(text));
        } else if (role == "assistant") {
            messages_.push_back(Message::makeAssistant(text));
        } else {
            messages_.push_back(Message::makeSystem(SystemSubtype::LOCAL_COMMAND, text));
        }
    }
    spdlog::info("Restored {} messages from session", messages_.size());
}

} // namespace closecrab
