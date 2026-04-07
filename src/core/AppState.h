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
    std::string effort = "medium";  // "low", "medium", "high"
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
