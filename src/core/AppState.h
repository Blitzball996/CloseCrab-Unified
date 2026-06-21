#pragma once

#include <string>
#include <map>
#include <set>
#include <atomic>
#include <mutex>
#include <cstdint>
#include "../permissions/PermissionEngine.h"
#include "CostTracker.h"

namespace closecrab {

// ============================================================
// Model usage tracking
// ============================================================

struct ModelUsage {
    int64_t inputTokens = 0;
    int64_t outputTokens = 0;
    int64_t cacheReadTokens = 0;
    int64_t cacheWriteTokens = 0;
    double costUSD = 0.0;
};

// ============================================================
// Thinking configuration
// ============================================================

struct ThinkingConfig {
    bool enabled = false;
    int budgetTokens = 10000;
    // Reasoning effort level (Claude Code 2.1.x native effort). One of:
    // "low" | "medium" | "high" | "xhigh" | "max". Default xhigh = "ultra"
    // (Claude Code's ultracode runs at xhigh). Sent to the API as
    // output_config.effort on effort-capable models (opus-4-6+, opus-4-8...).
    std::string effort = "xhigh";
};

// ============================================================
// AppState — 全局应用状态
// 对标 JackProAi BootstrapState + AppState
// ============================================================

struct AppState {
    // Project / Session
    std::string originalCwd;
    std::string projectRoot;
    std::string sessionId;
    std::string parentSessionId;

    // Model
    std::string currentModel;
    std::string fallbackModel;

    // Absolute path of the loaded config.yaml (so /api and the banner can show it
    // and write back to the right file on every OS).
    std::string configPath;

    // Cost tracking
    std::atomic<double> totalCostUSD{0.0};
    std::atomic<double> totalAPIDuration{0.0};
    std::atomic<double> totalToolDuration{0.0};
    std::map<std::string, ModelUsage> modelUsage;
    std::mutex usageMutex;

    // Modes
    PermissionMode permissionMode = PermissionMode::DEFAULT;
    bool planMode = false;
    bool fastMode = false;
    bool vimMode = false;
    bool voiceEnabled = false;
    bool coordinatorMode = false;  // claude-code coordinator: orchestrate workers only
    ThinkingConfig thinkingConfig;

    // Limits
    int maxTurns = 0;          // 0 = unlimited
    double maxBudgetUsd = 0.0; // 0 = unlimited

    // Flags
    bool verbose = false;
    bool bypassPermissionsAvailable = false;

    // CLAUDE.md content (cached)
    std::string claudeMdContent;

    // ---- Methods ----

    void trackUsage(const std::string& model, int64_t inputTokens, int64_t outputTokens,
                    int64_t cacheRead = 0, int64_t cacheWrite = 0) {
        std::lock_guard<std::mutex> lock(usageMutex);
        auto& u = modelUsage[model];
        u.inputTokens += inputTokens;
        u.outputTokens += outputTokens;
        u.cacheReadTokens += cacheRead;
        u.cacheWriteTokens += cacheWrite;

        // Also track in CostTracker for pricing calculation
        CostTracker::getInstance().track(model, inputTokens, outputTokens, cacheRead, cacheWrite);
        totalCostUSD.store(CostTracker::getInstance().getTotalCost());
    }

    double getTotalCost() const {
        return totalCostUSD.load();
    }
};

} // namespace closecrab
