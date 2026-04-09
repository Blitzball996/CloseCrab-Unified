#pragma once
#include <string>
#include <map>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace closecrab {
namespace fs = std::filesystem;

// Local feature flag system (no cloud dependency)
class FeatureFlags {
public:
    static FeatureFlags& getInstance() {
        static FeatureFlags instance;
        return instance;
    }

    // Check if a feature is enabled
    bool isEnabled(const std::string& flag) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flags_.find(flag);
        return it != flags_.end() && it->second;
    }

    // Set a feature flag
    void set(const std::string& flag, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        flags_[flag] = enabled;
        spdlog::debug("Feature flag '{}' = {}", flag, enabled);
    }

    // Toggle a feature flag
    bool toggle(const std::string& flag) {
        std::lock_guard<std::mutex> lock(mutex_);
        flags_[flag] = !flags_[flag];
        return flags_[flag];
    }

    // Get all flags
    std::map<std::string, bool> getAll() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return flags_;
    }

    // Load from .claude/feature-flags.json
    void loadFromFile(const std::string& projectRoot) {
        fs::path path = fs::path(projectRoot) / ".claude" / "feature-flags.json";
        if (!fs::exists(path)) return;
        try {
            std::ifstream f(path);
            auto j = nlohmann::json::parse(f);
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [key, val] : j.items()) {
                if (val.is_boolean()) flags_[key] = val.get<bool>();
            }
            spdlog::info("Loaded {} feature flags", flags_.size());
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load feature flags: {}", e.what());
        }
    }

    // Save to file
    void saveToFile(const std::string& projectRoot) {
        fs::path dir = fs::path(projectRoot) / ".claude";
        if (!fs::exists(dir)) fs::create_directories(dir);
        fs::path path = dir / "feature-flags.json";
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json j(flags_);
        std::ofstream f(path);
        f << j.dump(2);
    }

    // Predefined flag names
    static constexpr const char* REACTIVE_COMPACT = "reactive_compact";
    static constexpr const char* CONTEXT_COLLAPSE = "context_collapse";
    static constexpr const char* HISTORY_SNIP = "history_snip";
    static constexpr const char* MICRO_COMPACT = "micro_compact";
    static constexpr const char* SKILL_PREFETCH = "skill_prefetch";
    static constexpr const char* TEAM_MEMORY_SYNC = "team_memory_sync";
    static constexpr const char* AUTO_DREAM = "auto_dream";
    static constexpr const char* PROMPT_SUGGESTIONS = "prompt_suggestions";
    static constexpr const char* BUDGET_TRACKING = "budget_tracking";
    static constexpr const char* PREVENT_SLEEP = "prevent_sleep";

private:
    FeatureFlags() {
        // Defaults: all major features enabled
        flags_[REACTIVE_COMPACT] = true;
        flags_[CONTEXT_COLLAPSE] = true;
        flags_[HISTORY_SNIP] = true;
        flags_[MICRO_COMPACT] = true;
        flags_[SKILL_PREFETCH] = true;
        flags_[TEAM_MEMORY_SYNC] = false; // Requires endpoint
        flags_[AUTO_DREAM] = false;        // Opt-in
        flags_[PROMPT_SUGGESTIONS] = true;
        flags_[BUDGET_TRACKING] = true;
        flags_[PREVENT_SLEEP] = true;
    }

    mutable std::mutex mutex_;
    std::map<std::string, bool> flags_;
};

} // namespace closecrab
