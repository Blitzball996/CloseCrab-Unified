#pragma once

#include "../Tool.h"
#include "../../plugins/PluginManager.h"

namespace closecrab {

class SkillTool : public Tool {
public:
    std::string getName() const override { return "Skill"; }
    std::string getDescription() const override {
        return "Execute a user-defined skill from .claude/skills/ directory.";
    }
    std::string getCategory() const override { return "skill"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"skill",{{"type","string"},{"description","Skill name"}}},
            {"args",{{"type","string"},{"description","Optional arguments"}}}
        }},{"required",{"skill"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string skillName = input["skill"].get<std::string>();
        std::string args = input.value("args", "");

        auto& skillDir = SkillDirectory::getInstance();
        const auto* skill = skillDir.getSkill(skillName);
        if (!skill) {
            // List available skills
            auto names = skillDir.getSkillNames();
            std::string available = "Available skills: ";
            for (const auto& n : names) available += n + ", ";
            return ToolResult::fail("Skill not found: " + skillName + ". " + available);
        }

        // Build the skill prompt
        std::string fullPrompt = skill->prompt;
        if (!args.empty()) fullPrompt += "\n\nArguments: " + args;

        // Include references
        for (const auto& ref : skill->references) {
            fullPrompt += "\n\n--- Reference ---\n" + ref;
        }

        return ToolResult::ok(fullPrompt, {{"skill", skillName}, {"prompt_length", fullPrompt.size()}});
    }
};

} // namespace closecrab
