#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace closecrab {

// Skill definition loaded from .claude/skills/ directory
struct SkillDef {
    std::string name;
    std::string description;
    std::string prompt;         // Content of prompt.md
    std::string trigger;        // When to activate
    std::vector<std::string> references;  // Reference file contents
    std::string dirPath;        // Source directory
};

// Plugin definition
struct PluginDef {
    std::string name;
    std::string version;
    std::string description;
    std::string path;
    std::vector<std::string> commandNames;
    std::vector<std::string> skillNames;
    bool enabled = true;
};

// SkillDirectory — loads skills from .claude/skills/
class SkillDirectory {
public:
    static SkillDirectory& getInstance() {
        static SkillDirectory instance;
        return instance;
    }

    void loadFromDirectory(const std::string& projectRoot) {
        namespace fs = std::filesystem;
        std::lock_guard<std::mutex> lock(mutex_);
        skills_.clear();

        fs::path skillsDir = fs::path(projectRoot) / ".claude" / "skills";
        if (!fs::exists(skillsDir)) return;

        for (auto& entry : fs::directory_iterator(skillsDir)) {
            if (!entry.is_directory()) continue;
            loadSkill(entry.path());
        }
        spdlog::info("Loaded {} skills from {}", skills_.size(), skillsDir.string());
    }

    std::vector<SkillDef> getAllSkills() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SkillDef> result;
        for (const auto& [_, s] : skills_) result.push_back(s);
        return result;
    }

    const SkillDef* getSkill(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = skills_.find(name);
        return (it != skills_.end()) ? &it->second : nullptr;
    }

    std::vector<std::string> getSkillNames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [n, _] : skills_) names.push_back(n);
        return names;
    }

private:
    SkillDirectory() = default;

    void loadSkill(const std::filesystem::path& dir) {
        namespace fs = std::filesystem;
        SkillDef skill;
        skill.dirPath = dir.string();
        skill.name = dir.filename().string();

        // Read SKILL.md
        fs::path skillMd = dir / "SKILL.md";
        if (fs::exists(skillMd)) {
            std::ifstream f(skillMd);
            std::string content((std::istreambuf_iterator<char>(f)), {});
            // Parse frontmatter-style metadata
            if (content.substr(0, 3) == "---") {
                auto end = content.find("---", 3);
                if (end != std::string::npos) {
                    std::string meta = content.substr(3, end - 3);
                    // Simple parsing
                    for (std::istringstream iss(meta); std::getline(iss, meta); ) {
                        if (meta.find("description:") == 0)
                            skill.description = meta.substr(12);
                        if (meta.find("trigger:") == 0)
                            skill.trigger = meta.substr(8);
                    }
                }
            }
            if (skill.description.empty()) skill.description = content.substr(0, 100);
        }

        // Read prompt.md
        fs::path promptMd = dir / "prompt.md";
        if (fs::exists(promptMd)) {
            std::ifstream f(promptMd);
            skill.prompt = std::string((std::istreambuf_iterator<char>(f)), {});
        }

        // Read references/
        fs::path refsDir = dir / "references";
        if (fs::exists(refsDir) && fs::is_directory(refsDir)) {
            for (auto& ref : fs::directory_iterator(refsDir)) {
                if (ref.is_regular_file()) {
                    std::ifstream f(ref.path());
                    skill.references.push_back(
                        std::string((std::istreambuf_iterator<char>(f)), {}));
                }
            }
        }

        skills_[skill.name] = std::move(skill);
    }

    mutable std::mutex mutex_;
    std::map<std::string, SkillDef> skills_;
};

// PluginManager — manages plugins (simplified: directory-based)
class PluginManager {
public:
    static PluginManager& getInstance() {
        static PluginManager instance;
        return instance;
    }

    void loadFromDirectory(const std::string& projectRoot) {
        namespace fs = std::filesystem;
        std::lock_guard<std::mutex> lock(mutex_);
        plugins_.clear();

        fs::path pluginDir = fs::path(projectRoot) / ".claude" / "plugins";
        if (!fs::exists(pluginDir)) return;

        for (auto& entry : fs::directory_iterator(pluginDir)) {
            if (!entry.is_directory()) continue;
            loadPlugin(entry.path());
        }
        spdlog::info("Loaded {} plugins from {}", plugins_.size(), pluginDir.string());
    }

    std::vector<PluginDef> getAllPlugins() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PluginDef> result;
        for (const auto& [_, p] : plugins_) result.push_back(p);
        return result;
    }

    const PluginDef* getPlugin(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = plugins_.find(name);
        return (it != plugins_.end()) ? &it->second : nullptr;
    }

private:
    PluginManager() = default;

    void loadPlugin(const std::filesystem::path& dir) {
        namespace fs = std::filesystem;
        PluginDef plugin;
        plugin.name = dir.filename().string();
        plugin.path = dir.string();

        // Read manifest.json if exists
        fs::path manifest = dir / "manifest.json";
        if (fs::exists(manifest)) {
            try {
                std::ifstream f(manifest);
                auto j = nlohmann::json::parse(f);
                plugin.version = j.value("version", "0.0.0");
                plugin.description = j.value("description", "");
            } catch (...) {}
        }

        plugins_[plugin.name] = std::move(plugin);
    }

    mutable std::mutex mutex_;
    std::map<std::string, PluginDef> plugins_;
};

} // namespace closecrab
