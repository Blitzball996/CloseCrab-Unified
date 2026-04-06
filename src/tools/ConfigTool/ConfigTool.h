#pragma once

#include "../Tool.h"
#include "../../config/SettingsManager.h"

namespace closecrab {

class ConfigTool : public Tool {
public:
    std::string getName() const override { return "Config"; }
    std::string getDescription() const override {
        return "Read or modify configuration settings at runtime.";
    }
    std::string getCategory() const override { return "system"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"action",{{"type","string"},{"description","get, set, or list"}}},
            {"key",{{"type","string"},{"description","Setting key (dot notation)"}}},
            {"value",{{"description","Value to set (any JSON type)"}}}
        }},{"required",{"action"}}};
    }

    PermissionResult checkPermissions(const ToolContext&, const nlohmann::json& input) const override {
        std::string action = input.value("action", "");
        if (action == "get" || action == "list") return PermissionResult::ALLOWED;
        return PermissionResult::ASK_USER;
    }

    bool isReadOnly() const override { return false; }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        auto& settings = SettingsManager::getInstance();
        std::string action = input["action"].get<std::string>();

        if (action == "list") {
            return ToolResult::ok(settings.data().dump(2));
        }

        if (action == "get") {
            std::string key = input.value("key", "");
            if (key.empty()) return ToolResult::fail("Key required for 'get'");
            auto val = settings.getString(key, "");
            if (val.empty()) {
                // Try as JSON
                if (settings.data().contains(key)) {
                    return ToolResult::ok(settings.data()[key].dump(2));
                }
                return ToolResult::fail("Key not found: " + key);
            }
            return ToolResult::ok(val);
        }

        if (action == "set") {
            std::string key = input.value("key", "");
            if (key.empty()) return ToolResult::fail("Key required for 'set'");
            if (!input.contains("value")) return ToolResult::fail("Value required for 'set'");

            settings.set(key, input["value"]);
            settings.save();
            return ToolResult::ok("Set " + key + " = " + input["value"].dump());
        }

        return ToolResult::fail("Unknown action: " + action + ". Use get, set, or list.");
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Config " + input.value("action", "?") + " " + input.value("key", "");
    }
};

} // namespace closecrab
