#pragma once
#include <string>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "PermissionEngine.h"

namespace closecrab {

class PermissionPersistence {
public:
    static void save(const std::string& projectRoot) {
        namespace fs = std::filesystem;
        auto& engine = PermissionEngine::getInstance();
        nlohmann::json rules = engine.saveRules();

        fs::path dir = fs::path(projectRoot) / ".crab";
        fs::create_directories(dir);
        fs::path path = dir / "settings.json";

        // Merge with existing settings
        nlohmann::json settings;
        if (fs::exists(path)) {
            std::ifstream f(path);
            try { settings = nlohmann::json::parse(f); } catch (...) {}
        }
        settings["permissions"] = rules;

        std::ofstream out(path);
        if (out.is_open()) out << settings.dump(2);
    }

    static void load(const std::string& projectRoot) {
        namespace fs = std::filesystem;
        fs::path path = fs::path(projectRoot) / ".crab" / "settings.json";
        if (!fs::exists(path)) return;

        std::ifstream f(path);
        try {
            auto settings = nlohmann::json::parse(f);
            if (settings.contains("permissions")) {
                PermissionEngine::getInstance().loadRules(settings["permissions"]);
            }
        } catch (...) {}
    }
};

} // namespace closecrab
