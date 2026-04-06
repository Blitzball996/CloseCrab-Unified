#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <mutex>

namespace closecrab {

// Manages .claude/settings.json (user settings, permission rules, MCP config, hooks)
class SettingsManager {
public:
    static SettingsManager& getInstance();

    bool load(const std::string& projectRoot = ".");
    bool save() const;

    // Getters
    nlohmann::json getPermissionRules() const;
    nlohmann::json getMcpServers() const;
    nlohmann::json getHooks() const;
    std::string getString(const std::string& key, const std::string& def = "") const;
    bool getBool(const std::string& key, bool def = false) const;
    int getInt(const std::string& key, int def = 0) const;

    // Setters
    void setPermissionRules(const nlohmann::json& rules);
    void setMcpServers(const nlohmann::json& servers);
    void set(const std::string& key, const nlohmann::json& value);

    // Raw access
    const nlohmann::json& data() const { return data_; }
    std::string getFilePath() const { return filePath_; }

private:
    SettingsManager() = default;

    mutable std::mutex mutex_;
    nlohmann::json data_;
    std::string filePath_;
};

} // namespace closecrab
