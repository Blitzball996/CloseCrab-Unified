#pragma once

#include "../Tool.h"
#include "../../plugins/PluginManager.h"
#include "../../services/SkillSearchService.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace closecrab {

class DiscoverSkillsTool : public Tool {
public:
    std::string getName() const override { return "DiscoverSkills"; }
    std::string getDescription() const override {
        return "Discover available skills from local directories and remote registries.";
    }
    std::string getCategory() const override { return "ai"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Filter skills by name or description substring"}}},
                {"source", {{"type", "string"}, {"description", "Where to search: local, remote, or all (default: all)"},
                            {"enum", {"local", "remote", "all"}}}}
            }}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string query = input.value("query", "");
        std::string source = input.value("source", "all");

        // Normalize query to lowercase for case-insensitive matching
        std::string queryLower = query;
        std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        spdlog::debug("DiscoverSkills: query='{}' source='{}'", query, source);

        std::vector<SkillEntry> results;

        // Gather local skills
        if (source == "local" || source == "all") {
            auto& skillDir = SkillDirectory::getInstance();
            auto allSkills = skillDir.getAllSkills();
            for (const auto& skill : allSkills) {
                if (matchesQuery(skill.name, skill.description, queryLower)) {
                    results.push_back({skill.name, skill.description, skill.trigger, "local"});
                }
            }
        }

        // Gather remote skills from SkillSearchService prefetch cache
        if (source == "remote" || source == "all") {
            auto& searchSvc = SkillSearchService::getInstance();
            auto remoteSkills = searchSvc.search(query, 20);
            for (const auto& skill : remoteSkills) {
                // Avoid duplicates with local results
                bool dup = false;
                for (const auto& r : results) {
                    if (r.name == skill.name) { dup = true; break; }
                }
                if (!dup && matchesQuery(skill.name, skill.description, queryLower)) {
                    results.push_back({skill.name, skill.description, skill.trigger, "remote"});
                }
            }
            if (source == "remote" && results.empty()) {
                return ToolResult::ok("No remote skills found. "
                                      "Add a skill-registry.json in .claude/ or place skills in .claude/skills/.");
            }
        }

        if (results.empty()) {
            std::string msg = "No skills found";
            if (!query.empty()) msg += " matching \"" + query + "\"";
            msg += ". Place skill definitions in .claude/skills/ to make them discoverable.";
            return ToolResult::ok(msg);
        }

        // Format as table
        std::string table = formatTable(results);

        spdlog::info("DiscoverSkills: found {} skills", results.size());
        return ToolResult::ok(table, {{"count", results.size()}});
    }

private:
    struct SkillEntry {
        std::string name;
        std::string description;
        std::string trigger;
        std::string source;
    };

    static bool matchesQuery(const std::string& name, const std::string& description,
                             const std::string& queryLower) {
        if (queryLower.empty()) return true;

        std::string nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (nameLower.find(queryLower) != std::string::npos) return true;

        std::string descLower = description;
        std::transform(descLower.begin(), descLower.end(), descLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (descLower.find(queryLower) != std::string::npos) return true;

        return false;
    }

    static std::string formatTable(const std::vector<SkillEntry>& entries) {
        // Compute column widths
        size_t nameW = 4, descW = 11, trigW = 7, srcW = 6;
        for (const auto& e : entries) {
            nameW = std::max(nameW, e.name.size());
            descW = std::max(descW, std::min(e.description.size(), size_t(50)));
            trigW = std::max(trigW, std::min(e.trigger.size(), size_t(30)));
            srcW  = std::max(srcW, e.source.size());
        }

        std::ostringstream out;
        // Header
        out << std::left
            << std::setw(static_cast<int>(nameW + 2)) << "Name"
            << std::setw(static_cast<int>(descW + 2)) << "Description"
            << std::setw(static_cast<int>(trigW + 2)) << "Trigger"
            << "Source" << "\n";

        // Separator
        out << std::string(nameW, '-') << "  "
            << std::string(descW, '-') << "  "
            << std::string(trigW, '-') << "  "
            << std::string(srcW, '-') << "\n";

        // Rows
        for (const auto& e : entries) {
            std::string desc = e.description.size() > 50
                ? e.description.substr(0, 47) + "..."
                : e.description;
            std::string trig = e.trigger.size() > 30
                ? e.trigger.substr(0, 27) + "..."
                : e.trigger;

            out << std::left
                << std::setw(static_cast<int>(nameW + 2)) << e.name
                << std::setw(static_cast<int>(descW + 2)) << desc
                << std::setw(static_cast<int>(trigW + 2)) << (trig.empty() ? "(none)" : trig)
                << e.source << "\n";
        }

        out << "\n" << entries.size() << " skill(s) found.";
        return out.str();
    }
};

} // namespace closecrab
