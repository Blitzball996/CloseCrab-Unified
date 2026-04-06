#include "SettingsManager.h"
#include <fstream>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace closecrab {

SettingsManager& SettingsManager::getInstance() {
    static SettingsManager instance;
    return instance;
}

bool SettingsManager::load(const std::string& projectRoot) {
    std::lock_guard<std::mutex> lock(mutex_);
    filePath_ = (fs::path(projectRoot) / ".claude" / "settings.json").string();

    if (!fs::exists(filePath_)) {
        spdlog::info("No settings.json found at {}, using defaults", filePath_);
        data_ = nlohmann::json::object();
        return true;
    }

    try {
        std::ifstream f(filePath_);
        data_ = nlohmann::json::parse(f);
        spdlog::info("Loaded settings from {}", filePath_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to load settings.json: {}", e.what());
        data_ = nlohmann::json::object();
        return false;
    }
}

bool SettingsManager::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (filePath_.empty()) return false;

    try {
        fs::path dir = fs::path(filePath_).parent_path();
        if (!fs::exists(dir)) fs::create_directories(dir);

        std::ofstream f(filePath_);
        f << data_.dump(2);
        spdlog::debug("Saved settings to {}", filePath_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to save settings.json: {}", e.what());
        return false;
    }
}

nlohmann::json SettingsManager::getPermissionRules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.value("permissions", nlohmann::json::object());
}

nlohmann::json SettingsManager::getMcpServers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.value("mcpServers", nlohmann::json::object());
}

nlohmann::json SettingsManager::getHooks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.value("hooks", nlohmann::json::object());
}

std::string SettingsManager::getString(const std::string& key, const std::string& def) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.value(key, def);
}

bool SettingsManager::getBool(const std::string& key, bool def) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.value(key, def);
}

int SettingsManager::getInt(const std::string& key, int def) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.value(key, def);
}

void SettingsManager::setPermissionRules(const nlohmann::json& rules) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_["permissions"] = rules;
}

void SettingsManager::setMcpServers(const nlohmann::json& servers) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_["mcpServers"] = servers;
}

void SettingsManager::set(const std::string& key, const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_[key] = value;
}

} // namespace closecrab
