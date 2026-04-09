#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "../core/BudgetTracker.h"
#include "../hooks/HookManager.h"
#include "../utils/StringUtils.h"
#include "../api/APIClient.h"
#include "../core/QueryEngine.h"

#include <vector>
#include <future>
#include <mutex>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace closecrab {

// Result of a single tool execution with timing and interrupt metadata
struct ToolExecution {
    std::string toolName;
    std::string toolUseId;
    ToolResult result;
    double durationMs = 0.0;
    bool wasInterrupted = false;
};

// Streaming tool executor with budget trimming and parallel orchestration
class StreamingToolExecutor {
public:
    struct Config {
        int maxParallel = 4;
        int toolResultMaxTokens = 16000;
        bool enableBudgetTrimming = true;
    };

    explicit StreamingToolExecutor(const Config& config = {}) : config_(config) {}

    // Execute a batch of tool calls, returning results in order
    std::vector<ToolExecution> runTools(
        const std::vector<StreamEvent>& toolCalls,
        ToolContext& baseCtx,
        ToolRegistry* registry,
        BudgetTracker* budget,
        const QueryCallbacks& callbacks,
        std::atomic<bool>& interrupted)
    {
        if (toolCalls.empty()) return {};

        // Single tool: execute directly (no thread overhead)
        if (toolCalls.size() == 1) {
            auto exec = executeSingle(toolCalls[0], baseCtx, registry, budget, callbacks, interrupted);
            return { std::move(exec) };
        }

        // Multiple tools: parallel execution up to maxParallel
        return executeParallel(toolCalls, baseCtx, registry, budget, callbacks, interrupted);
    }

private:
    // Full lifecycle execution of a single tool call
    ToolExecution executeSingle(
        const StreamEvent& event,
        ToolContext& ctx,
        ToolRegistry* registry,
        BudgetTracker* budget,
        const QueryCallbacks& callbacks,
        std::atomic<bool>& interrupted)
    {
        ToolExecution exec;
        exec.toolName = event.toolName;
        exec.toolUseId = event.toolUseId;

        auto startTime = std::chrono::steady_clock::now();

        // 1. Resolve tool from registry
        Tool* tool = registry ? registry->getTool(event.toolName) : nullptr;
        if (!tool) {
            exec.result = ToolResult::fail("Unknown tool: " + event.toolName);
            exec.durationMs = elapsedMs(startTime);
            return exec;
        }

        // 2. Validate input
        auto validation = tool->validateInput(event.toolInput);
        if (!validation.valid) {
            std::string errMsg;
            for (const auto& e : validation.errors) {
                if (!errMsg.empty()) errMsg += "; ";
                errMsg += e;
            }
            exec.result = ToolResult::fail("Validation failed: " + errMsg);
            exec.durationMs = elapsedMs(startTime);
            return exec;
        }

        // 3. Permission check
        auto perm = tool->checkPermissions(ctx, event.toolInput);
        if (perm == PermissionResult::DENIED) {
            exec.result = ToolResult::fail("Permission denied for tool: " + event.toolName);
            exec.durationMs = elapsedMs(startTime);
            return exec;
        }
        // 3.5. If ASK_USER, invoke the permission callback
        if (perm == PermissionResult::ASK_USER) {
            if (callbacks.onAskPermission) {
                std::string desc = tool->getActivityDescription(event.toolInput);
                bool allowed = callbacks.onAskPermission(event.toolName, desc);
                if (!allowed) {
                    exec.result = ToolResult::fail("User denied permission for: " + event.toolName);
                    exec.durationMs = elapsedMs(startTime);
                    return exec;
                }
            }
        }

        // 4. Pre-hook
        auto& hookMgr = HookManager::getInstance();
        auto preHook = hookMgr.fire(HookEvent::PRE_TOOL_USE, event.toolName, event.toolInput);
        if (preHook.blocked) {
            exec.result = ToolResult::fail("Blocked by pre-hook: " + preHook.error);
            exec.durationMs = elapsedMs(startTime);
            return exec;
        }

        // 5. Notify UI of tool use
        if (callbacks.onToolUse) {
            callbacks.onToolUse(event.toolName, event.toolInput);
        }

        // 6. Execute the tool
        spdlog::debug("Executing tool: {} (id: {})", event.toolName, event.toolUseId);
        try {
            if (interrupted.load()) {
                exec.result = ToolResult::fail("Interrupted before execution");
                exec.wasInterrupted = true;
            } else {
                exec.result = tool->call(ctx, event.toolInput);
            }
        } catch (const std::exception& e) {
            exec.result = ToolResult::fail(std::string("Tool threw exception: ") + e.what());
            spdlog::error("Tool {} threw: {}", event.toolName, e.what());
        }

        // Check if interrupted during execution
        if (interrupted.load() && exec.result.success) {
            exec.wasInterrupted = true;
        }

        // 7. Post-hook
        hookMgr.fire(HookEvent::POST_TOOL_USE, event.toolName, {
            {"success", exec.result.success},
            {"content", exec.result.content}
        });

        // 8. Budget trimming on result content
        if (budget && config_.enableBudgetTrimming && exec.result.success) {
            exec.result.content = budget->applyToolResultBudget(exec.result.content);
        }

        // 9. Ensure UTF-8 safety
        if (exec.result.success) {
            exec.result.content = ensureUtf8(exec.result.content);
        } else {
            exec.result.error = ensureUtf8(exec.result.error);
        }

        // 10. Notify UI of result
        if (callbacks.onToolResult) {
            callbacks.onToolResult(event.toolName, exec.result);
        }

        exec.durationMs = elapsedMs(startTime);
        spdlog::debug("Tool {} completed in {:.1f}ms (success={})",
                      event.toolName, exec.durationMs, exec.result.success);
        return exec;
    }

    // Parallel execution of multiple tool calls
    std::vector<ToolExecution> executeParallel(
        const std::vector<StreamEvent>& toolCalls,
        ToolContext& ctx,
        ToolRegistry* registry,
        BudgetTracker* budget,
        const QueryCallbacks& callbacks,
        std::atomic<bool>& interrupted)
    {
        std::vector<std::future<ToolExecution>> futures;
        futures.reserve(toolCalls.size());

        int launched = 0;
        for (const auto& tc : toolCalls) {
            if (interrupted.load()) break;

            // Throttle: if we've hit maxParallel, wait for one to finish
            if (launched >= config_.maxParallel) {
                for (auto& f : futures) {
                    if (f.valid()) {
                        f.wait();
                        break;
                    }
                }
            }

            futures.push_back(std::async(std::launch::async,
                [this, &tc, &ctx, registry, budget, &callbacks, &interrupted]() {
                    return executeSingle(tc, ctx, registry, budget, callbacks, interrupted);
                }));
            launched++;
        }

        // Collect all results in order
        std::vector<ToolExecution> results;
        results.reserve(futures.size());
        for (auto& f : futures) {
            if (f.valid()) {
                results.push_back(f.get());
            }
        }
        return results;
    }

    static double elapsedMs(const std::chrono::steady_clock::time_point& start) {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    }

    Config config_;
};

} // namespace closecrab
