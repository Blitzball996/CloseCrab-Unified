#pragma once

#include <string>
#include <vector>
#include <map>
#include <regex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace closecrab {

// Hook events matching JackProAi's hook system
enum class HookEvent {
    PRE_TOOL_USE,       // Before a tool executes
    POST_TOOL_USE,      // After a tool executes
    NOTIFICATION,       // On notification events
    STOP,               // When generation stops
    POST_SAMPLING,      // After API response received
    STOP_FAILURE        // When stop hooks fail
};

inline std::string hookEventName(HookEvent e) {
    switch (e) {
        case HookEvent::PRE_TOOL_USE: return "PreToolUse";
        case HookEvent::POST_TOOL_USE: return "PostToolUse";
        case HookEvent::NOTIFICATION: return "Notification";
        case HookEvent::STOP: return "Stop";
        case HookEvent::POST_SAMPLING: return "PostSampling";
        case HookEvent::STOP_FAILURE: return "StopFailure";
    }
    return "Unknown";
}

inline HookEvent parseHookEvent(const std::string& s) {
    if (s == "PreToolUse") return HookEvent::PRE_TOOL_USE;
    if (s == "PostToolUse") return HookEvent::POST_TOOL_USE;
    if (s == "Notification") return HookEvent::NOTIFICATION;
    if (s == "Stop") return HookEvent::STOP;
    if (s == "PostSampling") return HookEvent::POST_SAMPLING;
    if (s == "StopFailure") return HookEvent::STOP_FAILURE;
    return HookEvent::POST_TOOL_USE; // default
}

struct HookDefinition {
    HookEvent event;
    std::string matcher;    // Tool/command name pattern (regex or exact)
    std::string command;    // Shell command to execute
    int timeout = 10000;    // Timeout in ms
};

struct HookResult {
    bool executed = false;
    bool blocked = false;   // Hook returned non-zero = block the action
    std::string output;
    std::string error;
    double durationMs = 0.0;
};

class HookManager {
public:
    static HookManager& getInstance() {
        static HookManager instance;
        return instance;
    }

    // Load hooks from settings.json format
    void loadFromSettings(const nlohmann::json& hooksConfig) {
        hooks_.clear();
        if (!hooksConfig.is_array()) return;

        for (const auto& h : hooksConfig) {
            HookDefinition def;
            def.event = parseHookEvent(h.value("event", "PostToolUse"));
            def.matcher = h.value("matcher", ".*");
            def.command = h.value("command", "");
            def.timeout = h.value("timeout", 10000);
            if (!def.command.empty()) {
                hooks_.push_back(std::move(def));
            }
        }
        spdlog::info("Loaded {} hooks", hooks_.size());
    }

    // Fire hooks for a given event and tool/action name
    // Returns combined results; if any hook blocks, blocked=true
    HookResult fire(HookEvent event, const std::string& toolName,
                    const nlohmann::json& context = {}) {
        HookResult combined;

        for (const auto& hook : hooks_) {
            if (hook.event != event) continue;

            // Check matcher
            try {
                std::regex pattern(hook.matcher);
                if (!std::regex_search(toolName, pattern)) continue;
            } catch (...) {
                // Exact match fallback
                if (hook.matcher != toolName && hook.matcher != ".*") continue;
            }

            // Build environment variables for the hook
            std::string envPrefix;
#ifdef _WIN32
            envPrefix = "set HOOK_TOOL=" + toolName + " && "
                      + "set HOOK_EVENT=" + hookEventName(event) + " && ";
#else
            envPrefix = "HOOK_TOOL='" + toolName + "' "
                      + "HOOK_EVENT='" + hookEventName(event) + "' ";
#endif

            // Execute hook command
            std::string fullCmd = envPrefix + hook.command;
            std::string output;
            int exitCode = executeHook(fullCmd, hook.timeout, output);

            combined.executed = true;
            if (!output.empty()) {
                combined.output += output;
            }

            if (exitCode != 0) {
                combined.blocked = true;
                combined.error = "Hook blocked: " + hook.command + " (exit " + std::to_string(exitCode) + ")";
                spdlog::warn("Hook blocked {} on {}: {}", hookEventName(event), toolName, combined.error);
                break; // First blocking hook stops execution
            }
        }

        return combined;
    }

    bool hasHooks() const { return !hooks_.empty(); }
    size_t hookCount() const { return hooks_.size(); }

    std::string listHooks() const {
        std::string out;
        for (const auto& h : hooks_) {
            out += hookEventName(h.event) + " [" + h.matcher + "] -> " + h.command + "\n";
        }
        return out.empty() ? "No hooks configured.\n" : out;
    }

private:
    HookManager() = default;

    int executeHook(const std::string& cmd, int timeoutMs, std::string& output) {
#ifdef _WIN32
        std::string fullCmd = "cmd /c \"" + cmd + "\" 2>&1";
        FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
        std::string fullCmd = cmd + " 2>&1";
        FILE* pipe = popen(fullCmd.c_str(), "r");
#endif
        if (!pipe) return -1;

        char buf[1024];
        while (fgets(buf, sizeof(buf), pipe) != nullptr) {
            output += buf;
            if (output.size() > 10240) break; // 10KB limit for hook output
        }

#ifdef _WIN32
        int exitCode = _pclose(pipe);
#else
        int exitCode = pclose(pipe);
#endif
        return exitCode;
    }

    std::vector<HookDefinition> hooks_;
};

} // namespace closecrab
