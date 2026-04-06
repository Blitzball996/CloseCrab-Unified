#pragma once

#include "../Tool.h"
#include <map>
#include <set>
#include <mutex>

namespace closecrab {

// In-memory team store
struct TeamEntry {
    std::string id;
    std::string name;
    std::string description;
    std::set<std::string> members;  // agent IDs or session IDs
    int64_t createdAt = 0;
};

class TeamStore {
public:
    static TeamStore& getInstance() {
        static TeamStore instance;
        return instance;
    }

    std::string create(const std::string& name, const std::string& description) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string id = "team_" + std::to_string(++nextId_);
        TeamEntry t;
        t.id = id;
        t.name = name;
        t.description = description;
        t.createdAt = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        teams_[id] = std::move(t);
        return id;
    }

    bool remove(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return teams_.erase(id) > 0;
    }

    TeamEntry* get(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = teams_.find(id);
        return (it != teams_.end()) ? &it->second : nullptr;
    }

    std::vector<TeamEntry> list() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TeamEntry> result;
        for (const auto& [_, t] : teams_) result.push_back(t);
        return result;
    }

private:
    TeamStore() = default;
    mutable std::mutex mutex_;
    std::map<std::string, TeamEntry> teams_;
    int nextId_ = 0;
};

class TeamCreateTool : public Tool {
public:
    std::string getName() const override { return "TeamCreate"; }
    std::string getDescription() const override {
        return "Create a team of agents for collaborative work.";
    }
    std::string getCategory() const override { return "team"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"name",{{"type","string"},{"description","Team name"}}},
            {"description",{{"type","string"},{"description","Team purpose"}}},
            {"members",{{"type","array"},{"items",{{"type","string"}}},
                        {"description","Agent IDs or types to include"}}}
        }},{"required",{"name"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string name = input["name"].get<std::string>();
        std::string desc = input.value("description", "");
        std::string id = TeamStore::getInstance().create(name, desc);

        // Add members if specified
        if (input.contains("members") && input["members"].is_array()) {
            auto* team = TeamStore::getInstance().get(id);
            if (team) {
                for (const auto& m : input["members"]) {
                    team->members.insert(m.get<std::string>());
                }
            }
        }

        return ToolResult::ok("Team created: " + id + " (" + name + ")");
    }
};

class TeamDeleteTool : public Tool {
public:
    std::string getName() const override { return "TeamDelete"; }
    std::string getDescription() const override { return "Delete a team."; }
    std::string getCategory() const override { return "team"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"team_id",{{"type","string"},{"description","Team ID to delete"}}}
        }},{"required",{"team_id"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string id = input["team_id"].get<std::string>();
        if (TeamStore::getInstance().remove(id)) {
            return ToolResult::ok("Team deleted: " + id);
        }
        return ToolResult::fail("Team not found: " + id);
    }
};

} // namespace closecrab
