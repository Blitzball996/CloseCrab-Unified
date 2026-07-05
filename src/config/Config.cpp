#include "Config.h"
#include <spdlog/spdlog.h>
#include <fstream>

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Split a dot-separated key into parts: "a.b.c" -> {"a","b","c"}
static std::vector<std::string> splitKey(const std::string& key) {
    std::vector<std::string> parts;
    std::string remaining = key;
    while (!remaining.empty()) {
        size_t dot = remaining.find('.');
        if (dot == std::string::npos) {
            parts.push_back(remaining);
            break;
        }
        parts.push_back(remaining.substr(0, dot));
        remaining = remaining.substr(dot + 1);
    }
    return parts;
}

// Walk the yaml-cpp node tree for the given parts.
// yaml-cpp Node has reference semantics: assigning "Node n = parent[key]" gives
// a reference into parent, NOT a copy. We create a local alias each step so we
// never re-assign the same variable (which could corrupt the parent in older
// yaml-cpp versions).
YAML::Node Config::getNode(const std::string& key) const {
    spdlog::debug("getNode: key='{}'", key);
    if (!root.IsMap()) return YAML::Node();

    auto parts = splitKey(key);
    YAML::Node cur = root;
    for (const auto& part : parts) {
        if (!cur.IsMap()) {
            spdlog::debug("getNode: '{}' is not a map", part);
            return YAML::Node();
        }
        if (!cur[part].IsDefined()) {
            spdlog::debug("getNode: part '{}' not found", part);
            return YAML::Node();
        }
        // Explicitly clone so each iteration has its own node object.
        cur = YAML::Node(cur[part]);
    }
    return cur;
}

// Set a value by dot-separated key, creating intermediate map nodes as needed.
void Config::setNode(const std::string& key, const std::string& value) {
    auto parts = splitKey(key);
    if (parts.empty()) return;

    // Walk / create intermediate nodes.
    // We build a "path" of references from the root so we can assign at the end.
    YAML::Node cur = root;
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        if (!cur[parts[i]].IsDefined() || !cur[parts[i]].IsMap()) {
            cur[parts[i]] = YAML::Node(YAML::NodeType::Map);
        }
        cur = cur[parts[i]];
    }
    cur[parts.back()] = value;
}

// ---------------------------------------------------------------------------
// load / save
// ---------------------------------------------------------------------------

bool Config::load(const std::string& filename) {
    try {
        configPath_ = filename;
        root = YAML::LoadFile(filename);
        spdlog::info("Config loaded from: {}", filename);

        if (root.IsMap()) {
            spdlog::info("Root keys after load:");
            for (auto it = root.begin(); it != root.end(); ++it)
                spdlog::info("  - {}", it->first.as<std::string>());
        }

        if (root["llm"] && root["llm"]["model_path"])
            spdlog::info("model_path = {}", root["llm"]["model_path"].as<std::string>());

        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to load config: {}", e.what());
        return false;
    }
}

bool Config::save() {
    if (configPath_.empty()) return false;
    try {
        std::ofstream f(configPath_, std::ios::trunc);
        if (!f) {
            spdlog::error("Config::save: cannot open '{}' for writing", configPath_);
            return false;
        }
        f << root;
        spdlog::info("Config saved to: {}", configPath_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Config::save failed: {}", e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

std::string Config::getString(const std::string& key, const std::string& defaultValue) const {
    YAML::Node node = getNode(key);
    spdlog::debug("getString: key={}, IsScalar={}", key, node.IsScalar());
    if (node && !node.IsNull() && node.IsScalar())
        return node.as<std::string>();
    return defaultValue;
}

int Config::getInt(const std::string& key, int defaultValue) const {
    YAML::Node node = getNode(key);
    if (node && !node.IsNull() && node.IsScalar())
        return node.as<int>();
    return defaultValue;
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    YAML::Node node = getNode(key);
    if (node && !node.IsNull() && node.IsScalar())
        return node.as<bool>();
    return defaultValue;
}

double Config::getDouble(const std::string& key, double defaultValue) const {
    YAML::Node node = getNode(key);
    if (node && !node.IsNull() && node.IsScalar())
        return node.as<double>();
    return defaultValue;
}

// ---------------------------------------------------------------------------
// Enumerate map children
// ---------------------------------------------------------------------------

std::vector<std::string> Config::getMapKeys(const std::string& prefix) const {
    std::vector<std::string> keys;
    YAML::Node node = prefix.empty() ? root : getNode(prefix);
    if (!node || !node.IsMap()) return keys;
    for (auto it = node.begin(); it != node.end(); ++it)
        keys.push_back(it->first.as<std::string>());
    return keys;
}

// ---------------------------------------------------------------------------
// Setters (write + persist)
// ---------------------------------------------------------------------------

bool Config::setString(const std::string& key, const std::string& value) {
    if (configPath_.empty()) {
        spdlog::warn("Config::setString called before load() — change not persisted");
        return false;
    }
    setNode(key, value);
    return save();
}
