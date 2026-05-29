#include "Config.h"
#include <spdlog/spdlog.h>

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

YAML::Node Config::getNode(const std::string& key) const {
    spdlog::debug("getNode: parsing key '{}'", key);
    if (!root.IsMap()) {
        spdlog::debug("Root is not a map");
        return YAML::Node();
    }

    // Use direct bracket access without reassigning Node references.
    // yaml-cpp Node has reference semantics — "Node current = root" creates
    // a reference, and "current = current[part]" can corrupt the original root
    // in some versions. Access via root["a"]["b"] directly instead.
    size_t dot = key.find('.');
    if (dot == std::string::npos) {
        // Simple key (no dots)
        if (root[key].IsDefined()) {
            return root[key];
        }
        spdlog::debug("getNode: part '{}' not found", key);
        return YAML::Node();
    }

    // Dotted key: split into parts and access nested
    std::string first = key.substr(0, dot);
    std::string rest = key.substr(dot + 1);
    if (!root[first].IsDefined() || !root[first].IsMap()) {
        spdlog::debug("getNode: part '{}' not found", first);
        return YAML::Node();
    }

    // Second level
    size_t dot2 = rest.find('.');
    if (dot2 == std::string::npos) {
        if (root[first][rest].IsDefined()) {
            return root[first][rest];
        }
        spdlog::debug("getNode: part '{}' not found in '{}'", rest, first);
        return YAML::Node();
    }

    // Third level (max depth we need)
    std::string second = rest.substr(0, dot2);
    std::string third = rest.substr(dot2 + 1);
    if (root[first][second].IsDefined() && root[first][second][third].IsDefined()) {
        return root[first][second][third];
    }
    spdlog::debug("getNode: deep key '{}' not found", key);
    return YAML::Node();
}

bool Config::load(const std::string& filename) {
    try {
        root = YAML::LoadFile(filename);
        spdlog::info("Config loaded from: {}", filename);

        // ��ӡ����
        if (root.IsMap()) {
            spdlog::info("Root keys after load:");
            for (auto it = root.begin(); it != root.end(); ++it) {
                spdlog::info("  - {}", it->first.as<std::string>());
            }
        }

        // ԭ�д�ӡ
        if (root["llm"]) {
            spdlog::info("llm node exists");
            if (root["llm"]["model_path"]) {
                spdlog::info("model_path = {}", root["llm"]["model_path"].as<std::string>());
            }
        }
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to load config: {}", e.what());
        return false;
    }
}

std::string Config::getString(const std::string& key, const std::string& defaultValue) const {
    YAML::Node node = getNode(key);
    spdlog::debug("getString: key={}, node.IsNull={}, node.IsScalar={}", key, node.IsNull(), node.IsScalar());
    if (node && !node.IsNull() && node.IsScalar()) {
        return node.as<std::string>();
    }
    return defaultValue;
}

int Config::getInt(const std::string& key, int defaultValue) const {
    YAML::Node node = getNode(key);
    if (node && !node.IsNull() && node.IsScalar()) {
        return node.as<int>();
    }
    return defaultValue;
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    YAML::Node node = getNode(key);
    if (node && !node.IsNull() && node.IsScalar()) {
        return node.as<bool>();
    }
    return defaultValue;
}

double Config::getDouble(const std::string& key, double defaultValue) const {
    YAML::Node node = getNode(key);
    if (node && !node.IsNull() && node.IsScalar()) {
        return node.as<double>();
    }
    return defaultValue;
}