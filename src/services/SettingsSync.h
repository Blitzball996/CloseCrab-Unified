#pragma once
#include <string>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace closecrab {

class SettingsSync {
public:
    static SettingsSync& getInstance() {
        static SettingsSync instance;
        return instance;
    }

    // Export settings to a portable JSON file
    bool exportSettings(const std::string& outputPath) {
        namespace fs = std::filesystem;
        nlohmann::json bundle;

        // Collect settings from various sources
        bundle["version"] = "1.0";
        bundle["exported_at"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Settings.json
        std::string settingsPath = ".crab/settings.json";
        if (fs::exists(settingsPath)) {
            std::ifstream f(settingsPath);
            try { bundle["settings"] = nlohmann::json::parse(f); } catch (...) {}
        }

        // Permission rules
        std::string permPath = "data/permissions.json";
        if (fs::exists(permPath)) {
            std::ifstream f(permPath);
            try { bundle["permissions"] = nlohmann::json::parse(f); } catch (...) {}
        }

        // Config (without secrets)
        bundle["preferences"] = {
            {"theme", "dark"},
            {"vim_mode", false},
            {"thinking_enabled", false}
        };

        std::ofstream out(outputPath);
        if (!out.is_open()) return false;
        out << bundle.dump(2);
        spdlog::info("Settings exported to: {}", outputPath);
        return true;
    }

    // Import settings from a portable JSON file
    bool importSettings(const std::string& inputPath) {
        namespace fs = std::filesystem;
        std::ifstream f(inputPath);
        if (!f.is_open()) return false;

        try {
            auto bundle = nlohmann::json::parse(f);

            // Restore settings.json
            if (bundle.contains("settings")) {
                fs::create_directories(".crab");
                std::ofstream out(".crab/settings.json");
                out << bundle["settings"].dump(2);
            }

            // Restore permissions
            if (bundle.contains("permissions")) {
                fs::create_directories("data");
                std::ofstream out("data/permissions.json");
                out << bundle["permissions"].dump(2);
            }

            spdlog::info("Settings imported from: {}", inputPath);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("Failed to import settings: {}", e.what());
            return false;
        }
    }

    // Get sync file path (for cloud sync via Dropbox/OneDrive/etc)
    static std::string getSyncPath() {
        // Check common cloud sync folders
        namespace fs = std::filesystem;
        std::vector<std::string> candidates = {
            std::string(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "") + "/OneDrive/.closecrab-sync.json",
            std::string(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "") + "/Dropbox/.closecrab-sync.json",
            std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.closecrab-sync.json"
        };
        for (const auto& path : candidates) {
            if (!path.empty() && fs::exists(fs::path(path).parent_path())) {
                return path;
            }
        }
        return "data/settings-sync.json";
    }

private:
    SettingsSync() = default;
};

} // namespace closecrab
