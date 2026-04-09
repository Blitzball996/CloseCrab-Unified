#pragma once
#include "../plugins/PluginManager.h"
#include <string>
#include <vector>
#include <mutex>
#include <future>
#include <optional>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {
namespace fs = std::filesystem;

class SkillSearchService {
public:
    static SkillSearchService& getInstance() {
        static SkillSearchService instance;
        return instance;
    }

    std::vector<SkillDef> search(const std::string& query, int maxResults = 10) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SkillDef> results;
        auto local = searchLocal(query);
        results.insert(results.end(), local.begin(), local.end());
        for (const auto& skill : prefetchedSkills_) {
            if (query.empty() || matchesQuery(skill, query)) results.push_back(skill);
        }
        std::sort(results.begin(), results.end(), [&](const SkillDef& a, const SkillDef& b) {
            return scoreRelevance(a, query) > scoreRelevance(b, query);
        });
        if (static_cast<int>(results.size()) > maxResults) results.resize(maxResults);
        return results;
    }

    void prefetch(const std::string& projectRoot) {
        prefetchFuture_ = std::async(std::launch::async, [this, projectRoot]() {
            fs::path regPath = fs::path(projectRoot) / ".claude" / "skill-registry.json";
            if (!fs::exists(regPath)) return;
            try {
                std::ifstream f(regPath);
                auto j = nlohmann::json::parse(f);
                std::lock_guard<std::mutex> lock(mutex_);
                prefetchedSkills_.clear();
                for (const auto& entry : j) {
                    SkillDef skill;
                    skill.name = entry.value("name", "");
                    skill.description = entry.value("description", "");
                    skill.prompt = entry.value("prompt", "");
                    skill.trigger = entry.value("trigger", "");
                    prefetchedSkills_.push_back(std::move(skill));
                }
                spdlog::info("Prefetched {} remote skills", prefetchedSkills_.size());
            } catch (const std::exception& e) {
                spdlog::warn("Failed to prefetch skills: {}", e.what());
            }
        });
    }

    std::optional<SkillDef> loadRemote(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& s : prefetchedSkills_) {
            if (s.name == name) return s;
        }
        return std::nullopt;
    }

private:
    SkillSearchService() = default;
    std::vector<SkillDef> searchLocal(const std::string& query) {
        auto& sd = SkillDirectory::getInstance();
        auto all = sd.getAllSkills();
        if (query.empty()) return all;
        std::vector<SkillDef> matched;
        for (const auto& s : all) { if (matchesQuery(s, query)) matched.push_back(s); }
        return matched;
    }
    bool matchesQuery(const SkillDef& s, const std::string& q) const {
        std::string lq = toLower(q);
        return toLower(s.name).find(lq) != std::string::npos
            || toLower(s.description).find(lq) != std::string::npos;
    }
    int scoreRelevance(const SkillDef& s, const std::string& q) const {
        if (q.empty()) return 0;
        std::string lq = toLower(q);
        int score = 0;
        if (toLower(s.name) == lq) score += 100;
        else if (toLower(s.name).find(lq) != std::string::npos) score += 50;
        if (toLower(s.description).find(lq) != std::string::npos) score += 20;
        return score;
    }
    static std::string toLower(const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(), ::tolower);
        return r;
    }
    std::mutex mutex_;
    std::future<void> prefetchFuture_;
    std::vector<SkillDef> prefetchedSkills_;
};

} // namespace closecrab
