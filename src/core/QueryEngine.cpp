#include "QueryEngine.h"
#include "../utils/UUID.h"
#include "../utils/StringUtils.h"
#include "../api/APIError.h"
#include "../hooks/HookManager.h"
#include "../tools/OutputPersistence.h"
#include "../memory/FileMemoryManager.h"
#include "ErrorRecovery.h"
#include "ContextCollapse.h"
#include "MessageSnip.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <future>
#include <mutex>
#include <regex>
#include <set>
#include <map>
#include <mutex>

namespace closecrab {

// Normalize LLM-generated tool names to actual registered names
static std::string normalizeToolName(const std::string& name) {
    static const std::map<std::string, std::string> aliases = {
        {"readFile", "Read"}, {"read_file", "Read"}, {"read", "Read"}, {"ReadFile", "Read"},
        {"writeFile", "Write"}, {"write_file", "Write"}, {"write", "Write"}, {"WriteFile", "Write"},
        {"editFile", "Edit"}, {"edit_file", "Edit"}, {"edit", "Edit"}, {"EditFile", "Edit"},
        {"bash", "Bash"}, {"runCommand", "Bash"}, {"run_command", "Bash"}, {"execute_command", "Bash"},
        {"glob", "Glob"}, {"globFiles", "Glob"}, {"glob_files", "Glob"}, {"list_directory", "Glob"},
        {"grep", "Grep"}, {"grepFiles", "Grep"}, {"grep_files", "Grep"}, {"search_files", "Grep"},
        {"webSearch", "WebSearch"}, {"web_search", "WebSearch"},
        {"webFetch", "WebFetch"}, {"web_fetch", "WebFetch"},
        {"create_file", "Write"}, {"overwrite_file", "Write"},
    };
    auto it = aliases.find(name);
    return it != aliases.end() ? it->second : name;
}

static nlohmann::json normalizeToolInput(const std::string& toolName, const nlohmann::json& input) {
    nlohmann::json out = input;
    if (out.contains("filePath") && !out.contains("file_path")) {
        out["file_path"] = out["filePath"]; out.erase("filePath");
    }
    if (out.contains("fileName") && !out.contains("file_path")) {
        out["file_path"] = out["fileName"]; out.erase("fileName");
    }
    if ((toolName == "Read" || toolName == "Write" || toolName == "Edit") &&
        out.contains("path") && !out.contains("file_path")) {
        out["file_path"] = out["path"]; out.erase("path");
    }
    return out;
}

QueryEngine::QueryEngine(const QueryEngineConfig& config)
    : config_(config), budgetTracker_(config.tokenBudget) {}

std::string QueryEngine::buildSystemPrompt() const {
    if (!systemPromptDirty_ && !cachedSystemPrompt_.empty()) {
        return cachedSystemPrompt_;
    }

    std::string prompt = config_.systemPrompt;

    // Core behavior rules (from claude-code system prompt):
    prompt += R"(

# Behavior Rules
- Default to action: implement changes rather than only suggesting them.
- When you know what to do, call tools immediately. Do NOT explain your plan first.
- If a task requires multiple files, write them one at a time in sequence.
- Keep text output minimal. The user wants results, not explanations.
- IMPORTANT: If the content to write exceeds 150 lines, you MUST only write the first 50 lines using the Write tool, then use the Edit tool to append the remaining content in chunks of no more than 50 lines each. If needed, leave a unique placeholder to help append content. On the final chunk, do NOT include the placeholder.
- Prefer the Edit tool for modifying existing files — it only sends the diff.
)";

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
        bool coordMode = config_.appState && config_.appState->coordinatorMode;
        for (Tool* t : config_.toolRegistry->getAllTools()) {
            if (!t->isEnabled() || t->isHidden()) continue;
            if (planMode && !t->isReadOnly()) continue;
            // Coordinator mode: only expose orchestration tools
            if (coordMode) {
                std::string name = t->getName();
                if (name != "Agent" && name != "TaskStop" && name != "AskUserQuestion"
                    && name != "TodoWrite") continue;
            }
            prompt += "- " + t->getName() + ": " + t->getDescription() + "\n";
        }
    }

    // For remote APIs: do NOT add fallback prompt — it confuses the model into using
    // text-based tool calls instead of native tool_use. JackProAi never adds this.
    // Native tool_use is sent via the `tools` field in the request body.

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
        if (config_.appState->coordinatorMode) prompt += "- Mode: COORDINATOR (orchestrate workers)\n";
    }

    // Coordinator mode: override system prompt with orchestration instructions
    // (claude-code coordinatorMode.ts getCoordinatorSystemPrompt)
    if (config_.appState && config_.appState->coordinatorMode) {
        prompt += R"(

## Coordinator Mode

You are a **coordinator**. Your job is to:
- Help the user achieve their goal
- Direct workers to research, implement and verify code changes
- Synthesize results and communicate with the user
- Answer questions directly when possible — don't delegate trivially

### Your Tools (coordinator-only)
- **Agent** — Spawn a new worker (use subagent_type "general-purpose")
- **TaskStop** — Stop a running worker

### Workers
Workers execute tasks autonomously. They have access to: Read, Write, Edit, Bash, Glob, Grep, WebSearch, WebFetch.

### Workflow
1. Analyze the user's request
2. Spawn workers for research/implementation/verification
3. After launching, tell the user what you launched and wait
4. When workers complete, synthesize results and report to user
5. Continue workers or spawn new ones as needed

### Rules
- Do NOT use Read/Write/Edit/Bash yourself — delegate to workers
- After spawning agents, end your response (don't predict results)
- Workers report back automatically when done
)";
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
    ModelConfig mc;
    // claude-code capped escalation strategy:
    // Start at 8K (saves slot reservation cost on short responses — most are <8K).
    // If the model hits max_tokens, the recovery logic below escalates to 64K.
    // This matches claude-code's DEFAULT_CAPPED_MAX_TOKENS=8000 + escalation to 64K.
    mc.maxTokens = cappedMaxTokens_;
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
            // JackProAi-style ToolSearch + defer_loading:
            // - Always-loaded tools (core) get full schema in initial request
            // - Deferred tools get full schema only after model calls ToolSearch with their name
            //   (we scan message history for "tool_reference" markers from ToolSearch results)
            // This keeps proxy-facing request small while exposing all tools via discovery.
            static const std::set<std::string> ALWAYS_LOAD = {
                "Read", "Write", "Edit", "Glob", "Grep", "Bash",
                "AskUserQuestion", "Agent", "WebSearch",
                "ToolSearch"  // ToolSearch itself must always be available
            };

            // Extract discovered tools from message history — JackProAi extractDiscoveredToolNames()
            std::set<std::string> discovered;
            for (const auto& msg : messages_) {
                for (const auto& block : msg.content) {
                    if (block.type != ContentBlockType::TOOL_RESULT) continue;
                    std::string text = block.toolResult.is_string()
                        ? block.toolResult.get<std::string>()
                        : block.toolResult.dump();
                    // Scan for <tool_reference name="X"/> markers
                    size_t pos = 0;
                    while ((pos = text.find("<tool_reference name=\"", pos)) != std::string::npos) {
                        pos += 22;
                        size_t end = text.find("\"", pos);
                        if (end == std::string::npos) break;
                        discovered.insert(text.substr(pos, end - pos));
                        pos = end + 1;
                    }
                }
            }

            bool planMode = config_.appState && config_.appState->planMode;
            bool coordMode = config_.appState && config_.appState->coordinatorMode;
            static const std::set<std::string> COORD_TOOLS = {"Agent", "TaskStop", "AskUserQuestion", "TodoWrite"};
            for (Tool* t : config_.toolRegistry->getAllTools()) {
                if (!t || !t->isEnabled() || t->isHidden()) continue;
                if (planMode && !t->isReadOnly()) continue;
                // Coordinator mode: only orchestration tools
                if (coordMode && COORD_TOOLS.find(t->getName()) == COORD_TOOLS.end()) continue;

                bool isAlwaysLoad = ALWAYS_LOAD.find(t->getName()) != ALWAYS_LOAD.end();
                bool isDiscovered = discovered.count(t->getName()) > 0;

                // Only send tool schema if it's always-loaded OR has been discovered via ToolSearch
                if (!isAlwaysLoad && !isDiscovered) continue;

                nlohmann::json def;
                def["name"] = t->getName();
                def["description"] = t->getDescription();
                def["input_schema"] = t->getInputSchema();
                toolDefs.push_back(std::move(def));
            }

            // For deferred (not-yet-discovered) tools: append their names to ToolSearch's
            // description so the model knows what's available to discover.
            // JackProAi does this via <available-deferred-tools> system reminder.
            std::vector<std::string> deferredNames;
            for (Tool* t : config_.toolRegistry->getAllTools()) {
                if (!t || !t->isEnabled() || t->isHidden()) continue;
                if (planMode && !t->isReadOnly()) continue;
                if (ALWAYS_LOAD.count(t->getName())) continue;
                if (discovered.count(t->getName())) continue;
                deferredNames.push_back(t->getName());
            }
            if (!deferredNames.empty()) {
                for (auto& def : toolDefs) {
                    if (def.value("name", "") == "ToolSearch") {
                        std::string extra = "\n\nAvailable deferred tools (call ToolSearch to load schema):\n";
                        for (const auto& n : deferredNames) extra += "- " + n + "\n";
                        def["description"] = def["description"].get<std::string>() + extra;
                        break;
                    }
                }
            }
        }

        mc.tools = std::move(toolDefs);
    }

    return mc;
}

void QueryEngine::processToolUse(const StreamEvent& event, const QueryCallbacks& callbacks) {
    if (!config_.toolRegistry) return;

    // Recursive-spawn guard: a sub-agent must not launch its own sub-agents.
    // Nested Agent spawning caused unbounded thread growth and crashed the
    // process (mirrors JackProAi's "fork not available inside a forked worker").
    if (!config_.allowSubagents && event.toolName == "Agent") {
        auto errResult = ToolResult::fail(
            "Sub-agents cannot spawn other agents. Complete the task with your own tools.");
        if (callbacks.onToolResult) callbacks.onToolResult(event.toolName, errResult);
        messages_.push_back(Message::makeToolResult(event.toolUseId,
            nlohmann::json(errResult.error), true));
        return;
    }

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
        // Denial tracking (claude-code pattern): after 3 denials of the same
        // tool, switch from auto-deny to always-ask so the user gets a chance
        // to approve if the model keeps trying.
        if (config_.permissionEngine) {
            config_.permissionEngine->trackDenial(event.toolName, event.toolInput.dump());
        }
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
    ctx.systemPrompt = buildSystemPrompt(); // For AgentTool cache sharing

    // Stream output callback for execution tools (Bash/PowerShell)
    if (callbacks.onToolUse) {
        ctx.onStreamOutput = [&callbacks, &event](const std::string& chunk) {
            // onProgress is used for streaming — UI can display in real-time
            if (callbacks.onToolResult) {
                // We don't call onToolResult here; streaming is handled by main.cpp's onStreamOutput
            }
        };
    }

    auto start = std::chrono::steady_clock::now();
    ToolResult result;
    { FILE* t = fopen("trace.log","a"); if(t){fprintf(t,"  tool-call %s input=%zu\n", event.toolName.c_str(), event.toolInput.dump().size()); fflush(t); fclose(t);} }
    try {
        result = tool->call(ctx, event.toolInput);
    } catch (const std::exception& e) {
        spdlog::error("Tool {} threw exception: {}", event.toolName, e.what());
        result = ToolResult::fail(std::string("Internal error: ") + e.what());
    } catch (...) {
        spdlog::error("Tool {} threw unknown exception", event.toolName);
        result = ToolResult::fail("Internal error (unknown exception)");
    }
    { FILE* t = fopen("trace.log","a"); if(t){fprintf(t,"  tool-ret %s ok=%d\n", event.toolName.c_str(), result.success?1:0); fflush(t); fclose(t);} }
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (config_.appState) {
        config_.appState->totalToolDuration.store(
            config_.appState->totalToolDuration.load() + elapsed);
    }

    result.elapsedSeconds = elapsed;

    { FILE* t = fopen("trace.log","a"); if(t){fprintf(t,"  pre-callback\n"); fflush(t); fclose(t);} }
    if (callbacks.onToolResult) callbacks.onToolResult(event.toolName, result);
    { FILE* t = fopen("trace.log","a"); if(t){fprintf(t,"  post-callback\n"); fflush(t); fclose(t);} }

    // Fire PostToolUse hooks
    if (hookMgr.hasHooks()) {
        hookMgr.fire(HookEvent::POST_TOOL_USE, event.toolName, event.toolInput);
    }

    // Add tool result to messages (ensure valid UTF-8 for JSON serialization)
    { FILE* t = fopen("trace.log","a"); if(t){fprintf(t,"  pre-msg-push content=%zu\n", result.content.size()); fflush(t); fclose(t);} }
    std::string safeContent = ensureUtf8(result.success ? result.content : result.error);
    // Persist large output to disk, return preview to LLM
    if (result.success) {
        safeContent = OutputPersistence::persistIfNeeded(safeContent, event.toolName, event.toolUseId);
    }
    safeContent = budgetTracker_.applyToolResultBudget(safeContent);
    // Use dump-safe json construction to avoid UTF-8 exceptions
    nlohmann::json resultJson;
    try {
        resultJson = nlohmann::json(safeContent);
    } catch (...) {
        // If json still rejects it, force-sanitize by replacing non-ASCII
        std::string ascii;
        for (char c : safeContent) {
            ascii += (static_cast<unsigned char>(c) < 0x80) ? c : '?';
        }
        resultJson = nlohmann::json(ascii);
    }
    messages_.push_back(Message::makeToolResult(event.toolUseId, resultJson, !result.success));
    { FILE* t = fopen("trace.log","a"); if(t){fprintf(t,"  tool-done %s ok=%d msgCount=%zu\n", event.toolName.c_str(), result.success?1:0, messages_.size()); fclose(t);} }
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

    // Loop detection: track recent tool calls to detect repetitive behavior
    struct ToolCallRecord {
        std::string toolName;
        std::string inputHash; // simplified hash of tool input
    };
    std::vector<ToolCallRecord> recentToolCalls;
    int consecutiveRepeatCount = 0;
    std::string lastAssistantText;
    const int kRepeatWarnThreshold = 4;   // inject warning after 4 consecutive repeats
    const int kRepeatBreakThreshold = 7;  // force break after 7 consecutive repeats

    // Same-tool-name streak detection (catches "Read different files in a loop" pattern)
    std::string lastToolNameSet;
    int sameToolNameStreak = 0;
    const int kSameToolWarnThreshold = 8;   // warn after 8 consecutive same-tool turns
    const int kSameToolBreakThreshold = 15; // force break after 15 consecutive same-tool turns

    // Multi-turn loop: keep going while LLM requests tool use
    while (turnCount < config_.maxTurns && !interrupted_) {
        turnCount++;

        // Budget check: stop if query token budget exceeded
        if (budgetTracker_.isQueryBudgetExceeded()) {
            spdlog::warn("Query token budget exceeded, stopping");
            break;
        }

        // Strategy-based compaction (check every 3 turns instead of 5 for better responsiveness)
        if (turnCount % 3 == 0) {
            compactor_.compactIfNeeded(messages_, config_.apiClient);
        }

        // Pre-flight size check: hybrid token estimation (like Claude Code/JackProAI)
        // Strategy: use last known real input_tokens from API + rough estimate for new messages
        {
            int64_t estimatedTokens = 0;

            if (lastKnownInputTokens_ > 0 && lastKnownTokensAtMessageIndex_ > 0) {
                // Hybrid: real count + estimate for messages added since last API call
                estimatedTokens = lastKnownInputTokens_;
                for (size_t i = lastKnownTokensAtMessageIndex_; i < messages_.size(); i++) {
                    for (const auto& block : messages_[i].content) {
                        if (block.type == ContentBlockType::TEXT || block.type == ContentBlockType::TOOL_RESULT) {
                            std::string text = block.text.empty() ?
                                (block.toolResult.is_string() ? block.toolResult.get<std::string>() : block.toolResult.dump()) : block.text;
                            estimatedTokens += (int64_t)text.size() / 4;  // Standard ratio for new messages
                        } else if (block.type == ContentBlockType::TOOL_USE) {
                            estimatedTokens += (int64_t)(block.toolName.size() + block.toolInput.dump().size()) / 4;
                        }
                    }
                }
            } else {
                // No real data yet: conservative full estimate
                for (const auto& msg : messages_) {
                    for (const auto& block : msg.content) {
                        if (block.type == ContentBlockType::TEXT || block.type == ContentBlockType::TOOL_RESULT) {
                            std::string text = block.text.empty() ?
                                (block.toolResult.is_string() ? block.toolResult.get<std::string>() : block.toolResult.dump()) : block.text;
                            estimatedTokens += (int64_t)text.size() / 3;
                        } else if (block.type == ContentBlockType::TOOL_USE) {
                            estimatedTokens += (int64_t)(block.toolName.size() + block.toolInput.dump().size()) / 3;
                        }
                    }
                }
            }

            // Context window limits: yikoulian.cc proxy supports full opus 200K.
            // Previous 30K/50K was overly conservative and caused complex tasks
            // Claude Opus 4.7 supports 1M (1,000,000) token context window.
            // Compact at 800K to leave room for output (64K) + tools + system prompt.
            // Hard stop at 950K to avoid API rejection.
            const int64_t AUTOCOMPACT_THRESHOLD = 800000;  // Compact at 800K
            const int64_t BLOCKING_LIMIT = 950000;         // Hard stop at 950K

            if (estimatedTokens > AUTOCOMPACT_THRESHOLD) {
                spdlog::warn("Pre-flight: {} tokens (>{} threshold), applying layered context management...",
                             estimatedTokens, AUTOCOMPACT_THRESHOLD);

                // Layer 1: Context Collapse (cheapest — preserves info in side map)
                int64_t collapseSavings = contextCollapse_.collapseOldMessages(messages_, 6);
                if (collapseSavings > 0) {
                    estimatedTokens -= collapseSavings;
                    spdlog::info("Pre-flight Layer 1 (Collapse): saved ~{} tokens, now ~{}",
                                 collapseSavings, estimatedTokens);
                }

                // Layer 2: Snip (medium — removes large blocks surgically)
                if (estimatedTokens > AUTOCOMPACT_THRESHOLD) {
                    int64_t snipTarget = estimatedTokens - static_cast<int64_t>(AUTOCOMPACT_THRESHOLD * 0.8);
                    auto snipResult = MessageSnip::snipLargest(messages_, snipTarget, 4);
                    if (snipResult.tokensFreed > 0) {
                        estimatedTokens -= snipResult.tokensFreed;
                        spdlog::info("Pre-flight Layer 2 (Snip): freed ~{} tokens, now ~{}",
                                     snipResult.tokensFreed, estimatedTokens);
                    }
                }

                // Layer 3: Force compact (expensive — summarizes everything via LLM)
                if (estimatedTokens > AUTOCOMPACT_THRESHOLD) {
                    spdlog::warn("Pre-flight Layer 3 (Compact): still at {} tokens, force compacting...",
                                 estimatedTokens);
                    compactor_.forceCompact(messages_, config_.apiClient);
                    contextCollapse_.reset();  // Compaction invalidates collapsed entries
                    lastKnownInputTokens_ = 0;
                    lastKnownTokensAtMessageIndex_ = 0;

                    // Recalculate after compaction
                    estimatedTokens = 0;
                    for (const auto& msg : messages_) {
                        for (const auto& block : msg.content) {
                            if (block.type == ContentBlockType::TEXT || block.type == ContentBlockType::TOOL_RESULT) {
                                std::string text = block.text.empty() ?
                                    (block.toolResult.is_string() ? block.toolResult.get<std::string>() : block.toolResult.dump()) : block.text;
                                estimatedTokens += (int64_t)text.size() / 3;
                            }
                        }
                    }
                }
            }

            // Layer 4: Blocking limit — aggressively truncate if still too large (last resort)
            if (estimatedTokens > BLOCKING_LIMIT) {
                spdlog::warn("Pre-flight Layer 4 (Truncate): {} tokens (>{} limit), hard truncating old tool results",
                             estimatedTokens, BLOCKING_LIMIT);
                auto truncResult = MessageSnip::snipOldToolResults(messages_, 4, 200);
                if (truncResult.tokensFreed > 0) {
                    estimatedTokens -= truncResult.tokensFreed;
                }
                // If still over, brute-force truncate remaining large text blocks
                if (estimatedTokens > BLOCKING_LIMIT) {
                    for (size_t i = 0; i + 4 < messages_.size(); i++) {
                        for (auto& block : messages_[i].content) {
                            if (block.type == ContentBlockType::TEXT && block.text.size() > 200) {
                                block.text = block.text.substr(0, 150) + "\n[truncated]";
                            }
                        }
                    }
                }
            }
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
                        // Budget tracking
                        if (event.usage.outputTokens > 0) {
                            budgetTracker_.consumeQueryTokens(event.usage.inputTokens + event.usage.outputTokens);
                            budgetTracker_.consumeTaskTokens(event.usage.inputTokens + event.usage.outputTokens);
                        }
                        break;

                    case StreamEvent::EVT_USAGE_UPDATE:
                        // Store real token count for accurate pre-flight checks (like JackProAI)
                        if (event.usage.inputTokens > 0) {
                            lastKnownInputTokens_ = event.usage.inputTokens;
                            lastKnownTokensAtMessageIndex_ = (int)messages_.size();
                        }
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

                    case StreamEvent::EVT_RETRY:
                        if (callbacks.onRetry)
                            callbacks.onRetry(event.retryAttempt, event.retryMax,
                                              event.retryDelayMs, event.content);
                        break;
                }
            }
        );
        } catch (const APIError& e) {
            if (ErrorRecovery::isPromptTooLong(e)) {
                auto recovery = ErrorRecovery::handlePromptTooLong(messages_, config_.apiClient, compactor_);
                if (recovery.success) {
                    spdlog::info("Recovered from prompt too long: {}", recovery.reason);
                    continue; // Retry the turn
                }
            }
            spdlog::debug("API call failed: {}", e.what());
            if (callbacks.onError) {
                if (e.type == APIErrorType::OVERLOADED || e.type == APIErrorType::SERVER_ERROR) {
                    callbacks.onError("API temporarily unavailable, please try again.");
                } else {
                    callbacks.onError(e.what());
                }
            }
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

        { FILE* t = fopen("trace.log","a"); if(t){fprintf(t,"[turn%d] post-stream stopReason=%s tools=%zu text=%zu\n", turnCount, stopReason.c_str(), pendingToolCalls.size(), accumulatedText.size()); fclose(t);} }

        // Error recovery: handle max_tokens stop reason
        // claude-code escalation: if capped at 8K and hit the limit, escalate to 64K and retry.
        if (stopReason == "max_tokens") {
            if (cappedMaxTokens_ < ESCALATED_MAX_TOKENS) {
                spdlog::info("max_tokens hit at {}; escalating to {}", cappedMaxTokens_, ESCALATED_MAX_TOKENS);
                cappedMaxTokens_ = ESCALATED_MAX_TOKENS;
                // Don't add the incomplete message — retry with higher limit
                continue;
            }
            // Already at max — try existing recovery (compact + retry)
            if (!pendingToolCalls.empty()) {
                static int maxTokensRecoveryAttempt = 0;
                auto recovery = ErrorRecovery::handleMaxOutputTokens(
                    messages_, config_.apiClient, compactor_, ++maxTokensRecoveryAttempt);
                if (recovery.success) {
                    spdlog::info("Recovered from max_tokens: {}", recovery.reason);
                    continue;
                }
                maxTokensRecoveryAttempt = 0;
            }
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

        // Process tool calls (parallel when multiple, sequential when single)
        if (!pendingToolCalls.empty() && !interrupted_) {
            {
                FILE* t = fopen("trace.log","a");
                if(t){
                    fprintf(t,"[turn%d] pre-tool count=%zu\n", turnCount, pendingToolCalls.size());
                    for (size_t i = 0; i < pendingToolCalls.size(); i++) {
                        fprintf(t,"  tool[%zu]=%s id=%s input=%zu\n", i,
                            pendingToolCalls[i].toolName.c_str(),
                            pendingToolCalls[i].toolUseId.c_str(),
                            pendingToolCalls[i].toolInput.dump().size());
                    }
                    fprintf(t,"  messages=%zu\n", messages_.size());
                    fflush(t); fclose(t);
                }
            }
            if (pendingToolCalls.size() == 1) {
                // Single tool: execute directly
                processToolUse(pendingToolCalls[0], callbacks);
            } else {
                // Partition tools into serial and parallel
                std::vector<StreamEvent> parallelCalls;
                std::vector<StreamEvent> serialCalls;
                { FILE* t = fopen("trace.log","a"); if(t){fprintf(t,"  partitioning %zu tools\n", pendingToolCalls.size()); fflush(t); fclose(t);} }
                for (const auto& tc : pendingToolCalls) {
                    Tool* t = config_.toolRegistry ? config_.toolRegistry->getTool(tc.toolName) : nullptr;
                    if (t && t->isConcurrencySafe()) parallelCalls.push_back(tc);
                    else serialCalls.push_back(tc);
                }

                // Stateful tools first, sequentially (full permission + display).
                for (const auto& tc : serialCalls) {
                    if (interrupted_) break;
                    processToolUse(tc, callbacks);
                }

                // Concurrency-safe tools in parallel.
                // CRITICAL: do NOT call callbacks.onToolUse/onToolResult from worker
                // threads — the callbacks touch the Spinner (thread join/start) which
                // causes data races → segfault. Collect results, display after join.
                struct ParallelResult {
                    StreamEvent event;
                    ToolResult result;
                };
                std::vector<std::future<ParallelResult>> futures;
                std::mutex messagesMutex;

                for (const auto& tc : parallelCalls) {
                    if (interrupted_) break;
                    futures.push_back(std::async(std::launch::async, [&, tc]() -> ParallelResult {
                        if (interrupted_) return {tc, ToolResult::fail("interrupted")};
                        StreamEvent localEvent = tc;

                        if (!config_.toolRegistry) return {localEvent, ToolResult::fail("no registry")};
                        Tool* tool = config_.toolRegistry->getTool(localEvent.toolName);
                        if (!tool) return {localEvent, ToolResult::fail("Unknown tool: " + localEvent.toolName)};

                        ToolContext ctx;
                        ctx.cwd = config_.cwd;
                        ctx.messages = &messages_;
                        ctx.appState = config_.appState;
                        ctx.permissionEngine = config_.permissionEngine;
                        ctx.abortFlag = &interrupted_;
                        ctx.apiClient = config_.apiClient;
                        ctx.toolRegistry = config_.toolRegistry;

                        ToolResult result;
                        try {
                            result = tool->call(ctx, localEvent.toolInput);
                        } catch (const std::exception& e) {
                            result = ToolResult::fail(std::string("Internal error: ") + e.what());
                        } catch (...) {
                            result = ToolResult::fail("Internal error (unknown exception)");
                        }
                        return {localEvent, result};
                    }));
                }

                // Wait for all parallel tools, then display results sequentially (safe)
                for (auto& f : futures) {
                    if (!f.valid()) continue;
                    auto [ev, result] = f.get();
                    if (callbacks.onToolUse) callbacks.onToolUse(ev.toolName, ev.toolInput);
                    if (callbacks.onToolResult) callbacks.onToolResult(ev.toolName, result);

                    std::string safeContent = ensureUtf8(result.success ? result.content : result.error);
                    nlohmann::json resultJson;
                    try { resultJson = nlohmann::json(safeContent); }
                    catch (...) {
                        std::string ascii;
                        for (char c : safeContent) ascii += (static_cast<unsigned char>(c) < 0x80) ? c : '?';
                        resultJson = nlohmann::json(ascii);
                    }
                    messages_.push_back(Message::makeToolResult(ev.toolUseId, resultJson, !result.success));
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

            // --- Loop detection: check for repetitive tool calls ---
            {
                // Build a simple signature for this turn's tool calls
                std::string turnSignature;
                std::string turnToolNames;
                for (const auto& tc : pendingToolCalls) {
                    turnToolNames += tc.toolName + ";";
                    turnSignature += tc.toolName + ":";
                    // Use a short prefix of the input dump as a fingerprint
                    std::string inputDump = tc.toolInput.dump();
                    if (inputDump.size() > 128) inputDump = inputDump.substr(0, 128);
                    turnSignature += inputDump + ";";
                }

                // Check if the assistant text is substantially similar to last turn
                // (strip whitespace for comparison)
                std::string trimmedText = accumulatedText;
                trimmedText.erase(0, trimmedText.find_first_not_of(" \t\r\n"));
                if (!trimmedText.empty()) {
                    trimmedText.erase(trimmedText.find_last_not_of(" \t\r\n") + 1);
                }

                bool isRepeat = false;
                if (!recentToolCalls.empty()) {
                    // Check if same tool(s) called with same input as last turn
                    std::string lastSig;
                    std::string lastToolNames;
                    for (const auto& r : recentToolCalls) {
                        lastSig += r.toolName + ":" + r.inputHash + ";";
                        lastToolNames += r.toolName + ";";
                    }
                    // Exact same tool call (same tool + same input)
                    if (turnSignature == lastSig) {
                        isRepeat = true;
                    }
                    // Same tool name(s) with similar assistant text (different files but same pattern)
                    else if (turnToolNames == lastToolNames && !trimmedText.empty() && !lastAssistantText.empty()) {
                        if (trimmedText == lastAssistantText ||
                            (trimmedText.size() > 5 && lastAssistantText.size() > 5 &&
                             trimmedText.find(lastAssistantText.substr(0, std::min<size_t>(lastAssistantText.size(), 20))) != std::string::npos)) {
                            isRepeat = true;
                        }
                    }
                }

                // Update tracking state
                recentToolCalls.clear();
                for (const auto& tc : pendingToolCalls) {
                    std::string inputDump = tc.toolInput.dump();
                    if (inputDump.size() > 128) inputDump = inputDump.substr(0, 128);
                    recentToolCalls.push_back({tc.toolName, inputDump});
                }
                lastAssistantText = trimmedText;

                if (isRepeat) {
                    consecutiveRepeatCount++;
                } else {
                    consecutiveRepeatCount = 0;
                }

                // Same-tool-name streak: catches "Read different files endlessly" pattern
                if (turnToolNames == lastToolNameSet) {
                    sameToolNameStreak++;
                } else {
                    sameToolNameStreak = 0;
                }
                lastToolNameSet = turnToolNames;

                // Force break if too many repeats (either exact repeat or same-tool streak)
                if (consecutiveRepeatCount >= kRepeatBreakThreshold ||
                    sameToolNameStreak >= kSameToolBreakThreshold) {
                    spdlog::warn("Loop detected: repeatCount={}, sameToolStreak={}, forcing stop",
                                 consecutiveRepeatCount, sameToolNameStreak);
                    if (callbacks.onText) {
                        callbacks.onText("\n\n[System: Detected repetitive loop — stopping to avoid wasting tokens. "
                                         "Please try a more specific request or break the task into smaller steps.]\n");
                    }
                    break;
                }

                // Inject warning to nudge the model out of the loop
                if (consecutiveRepeatCount >= kRepeatWarnThreshold ||
                    sameToolNameStreak >= kSameToolWarnThreshold) {
                    spdlog::warn("Possible loop: repeatCount={}, sameToolStreak={}, injecting warning",
                                 consecutiveRepeatCount, sameToolNameStreak);
                    std::string warning =
                        "[SYSTEM WARNING] You appear to be stuck in a repetitive loop — you have called the same tool(s) "
                        "many times in a row. STOP repeating the same action. Instead: "
                        "1) Summarize what you have gathered so far. "
                        "2) Proceed to complete the user's original request with the information you already have. "
                        "3) If you need more data, use a DIFFERENT approach (e.g., Glob to list files, then read only the ones you haven't read yet).";
                    messages_.push_back(Message::makeUser(warning));
                }
            }

            // Remote API: continue the loop naturally
            continue;
        }

        // No native tool calls — check for text-based tool formats
        if (!accumulatedText.empty()) {
            std::string toolName, toolUseId;
            nlohmann::json toolInput;
            bool foundTool = false;

            // Format 1: SKILL: <name>\nPARAMS: <json>
            if (accumulatedText.find("SKILL:") != std::string::npos) {
                auto pos = accumulatedText.find("SKILL:");
                auto nameStart = pos + 6;
                while (nameStart < accumulatedText.size() && accumulatedText[nameStart] == ' ') nameStart++;
                auto nameEnd = accumulatedText.find_first_of(" \t\r\n", nameStart);
                if (nameEnd != std::string::npos) {
                    toolName = accumulatedText.substr(nameStart, nameEnd - nameStart);
                    auto paramsPos = accumulatedText.find("PARAMS:", nameEnd);
                    if (paramsPos != std::string::npos) {
                        auto jsonStart = accumulatedText.find('{', paramsPos);
                        if (jsonStart != std::string::npos) {
                            int depth = 0; size_t jsonEnd = jsonStart;
                            for (size_t i = jsonStart; i < accumulatedText.size(); i++) {
                                if (accumulatedText[i] == '{') depth++;
                                else if (accumulatedText[i] == '}') { depth--; if (depth == 0) { jsonEnd = i + 1; break; } }
                            }
                            try { toolInput = nlohmann::json::parse(accumulatedText.substr(jsonStart, jsonEnd - jsonStart)); foundTool = true; }
                            catch (...) {}
                        }
                    }
                }
            }

            // Format 2: <tool_name>X</tool_name>...<tool_input>{...}</tool_input>
            if (!foundTool && accumulatedText.find("<tool_name>") != std::string::npos) {
                auto ns = accumulatedText.find("<tool_name>") + 11;
                auto ne = accumulatedText.find("</tool_name>", ns);
                if (ne != std::string::npos) {
                    toolName = accumulatedText.substr(ns, ne - ns);
                    while (!toolName.empty() && (toolName.front() == ' ' || toolName.front() == '\n')) toolName.erase(0, 1);
                    while (!toolName.empty() && (toolName.back() == ' ' || toolName.back() == '\n')) toolName.pop_back();
                    auto is = accumulatedText.find("<tool_input>", ne);
                    auto ie = accumulatedText.find("</tool_input>", is);
                    if (is != std::string::npos && ie != std::string::npos) {
                        std::string inp = accumulatedText.substr(is + 12, ie - is - 12);
                        while (!inp.empty() && (inp.front() == ' ' || inp.front() == '\n')) inp.erase(0, 1);
                        while (!inp.empty() && (inp.back() == ' ' || inp.back() == '\n')) inp.pop_back();
                        try { toolInput = nlohmann::json::parse(inp); foundTool = true; } catch (...) {}
                    }
                }
            }

            // Format 3: <tool_call>{"name":"X","arguments":{...}}</tool_call>
            if (!foundTool && accumulatedText.find("<tool_call>") != std::string::npos) {
                auto s = accumulatedText.find("<tool_call>") + 11;
                auto e = accumulatedText.find("</tool_call>", s);
                if (e != std::string::npos) {
                    std::string c = accumulatedText.substr(s, e - s);
                    while (!c.empty() && (c.front() == ' ' || c.front() == '\n')) c.erase(0, 1);
                    while (!c.empty() && (c.back() == ' ' || c.back() == '\n')) c.pop_back();
                    try {
                        auto j = nlohmann::json::parse(c);
                        toolName = j.value("name", "");
                        toolInput = j.value("arguments", j.value("parameters", j.value("input", nlohmann::json::object())));
                        if (!toolName.empty()) foundTool = true;
                    } catch (...) {}
                }
            }

            // Format 4: <tool_use> variants
            if (!foundTool && accumulatedText.find("<tool_use>") != std::string::npos) {
                auto us = accumulatedText.find("<tool_use>");
                auto ue = accumulatedText.find("</tool_use>", us);
                if (ue != std::string::npos) {
                    std::string block = accumulatedText.substr(us + 10, ue - us - 10);
                    auto tns = block.find("<tool_name>"); auto tne = block.find("</tool_name>");
                    if (tns != std::string::npos && tne != std::string::npos) {
                        toolName = block.substr(tns + 11, tne - tns - 11);
                        auto as = block.find("<arguments>"); auto ae = block.find("</arguments>");
                        if (as != std::string::npos && ae != std::string::npos) {
                            std::string a = block.substr(as + 11, ae - as - 11);
                            while (!a.empty() && (a.front() == ' ' || a.front() == '\n')) a.erase(0, 1);
                            while (!a.empty() && (a.back() == ' ' || a.back() == '\n')) a.pop_back();
                            try { toolInput = nlohmann::json::parse(a); } catch (...) {}
                        }
                        if (!toolName.empty()) foundTool = true;
                    }
                    if (!foundTool) {
                        auto js = block.find('{');
                        if (js != std::string::npos) {
                            std::string jstr = block.substr(js);
                            while (!jstr.empty() && (jstr.back() == ' ' || jstr.back() == '\n')) jstr.pop_back();
                            try {
                                auto j = nlohmann::json::parse(jstr);
                                toolName = j.value("name", "");
                                toolInput = j.value("parameters", j.value("arguments", j.value("input", nlohmann::json::object())));
                                if (!toolName.empty()) foundTool = true;
                            } catch (...) {}
                        }
                    }
                }
            }

            if (foundTool && !toolName.empty()) {
                toolName = normalizeToolName(toolName);
                toolInput = normalizeToolInput(toolName, toolInput);
                toolUseId = "text_" + std::to_string(turnCount);
                spdlog::info("Text-based tool call detected: {} with input: {}", toolName, toolInput.dump());

                // Fix assistant message: replace hallucinated content with tool_use block
                if (!messages_.empty() && messages_.back().role == MessageRole::ASSISTANT) {
                    messages_.pop_back();
                    Message fixedMsg;
                    fixedMsg.type = MessageType::ASSISTANT;
                    fixedMsg.role = MessageRole::ASSISTANT;
                    fixedMsg.uuid = generateUUID();
                    fixedMsg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    ContentBlock block;
                    block.type = ContentBlockType::TOOL_USE;
                    block.toolName = toolName;
                    block.toolUseId = toolUseId;
                    block.toolInput = toolInput;
                    fixedMsg.content.push_back(std::move(block));
                    messages_.push_back(std::move(fixedMsg));
                }

                StreamEvent toolEvent;
                toolEvent.type = StreamEvent::EVT_TOOL_USE;
                toolEvent.toolName = toolName;
                toolEvent.toolUseId = toolUseId;
                toolEvent.toolInput = toolInput;
                processToolUse(toolEvent, callbacks);

                // --- Loop detection for text-based tool calls ---
                {
                    std::string inputDump = toolInput.dump();
                    if (inputDump.size() > 128) inputDump = inputDump.substr(0, 128);
                    std::string turnSig = toolName + ":" + inputDump + ";";

                    std::string lastSig;
                    for (const auto& r : recentToolCalls) {
                        lastSig += r.toolName + ":" + r.inputHash + ";";
                    }

                    bool isRepeat = (!recentToolCalls.empty() && turnSig == lastSig);

                    recentToolCalls.clear();
                    recentToolCalls.push_back({toolName, inputDump});

                    if (isRepeat) {
                        consecutiveRepeatCount++;
                    } else {
                        consecutiveRepeatCount = 0;
                    }

                    if (consecutiveRepeatCount >= kRepeatBreakThreshold) {
                        spdlog::warn("Loop detected (text-based): {} consecutive repetitive tool calls, forcing stop", consecutiveRepeatCount);
                        if (callbacks.onText) {
                            callbacks.onText("\n\n[System: Detected repetitive loop — stopping to avoid wasting tokens. "
                                             "Please try a more specific request or break the task into smaller steps.]\n");
                        }
                        break;
                    }

                    if (consecutiveRepeatCount >= kRepeatWarnThreshold) {
                        spdlog::warn("Possible loop detected (text-based): {} consecutive similar tool calls", consecutiveRepeatCount);
                        std::string warning =
                            "[SYSTEM WARNING] You are stuck in a repetitive loop — you have called " + toolName +
                            " " + std::to_string(consecutiveRepeatCount) + " times in a row. "
                            "STOP repeating. Summarize what you have so far and complete the user's request.";
                        messages_.push_back(Message::makeUser(warning));
                    }
                }

                continue;
            }
        }

        // No tool calls — we're done
        break;
    }

    budgetTracker_.resetQueryBudget();
    if (callbacks.onComplete) callbacks.onComplete();
}

void QueryEngine::interrupt() {
    interrupted_ = true;
}

CompactMetadata QueryEngine::getLastCompactMetadata() const {
    return compactor_.getLastMetadata();
}

void QueryEngine::setBudget(const TokenBudget& budget) {
    budgetTracker_.setBudget(budget);
    budgetTracker_.resetQueryBudget();
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
