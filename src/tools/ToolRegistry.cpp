#include "ToolRegistry.h"
#include <spdlog/spdlog.h>

namespace closecrab {

ToolRegistry& ToolRegistry::getInstance() {
    static ToolRegistry instance;
    return instance;
}

void ToolRegistry::registerTool(std::unique_ptr<Tool> tool) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string name = tool->getName();
    auto aliases = tool->getAliases();

    spdlog::debug("Registering tool: {}", name);
    tools_[name] = std::move(tool);

    for (const auto& alias : aliases) {
        aliases_[alias] = name;
    }
}

Tool* ToolRegistry::getTool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Direct lookup
    auto it = tools_.find(name);
    if (it != tools_.end()) return it->second.get();

    // Alias lookup
    auto ait = aliases_.find(name);
    if (ait != aliases_.end()) {
        it = tools_.find(ait->second);
        if (it != tools_.end()) return it->second.get();
    }
    return nullptr;
}

std::vector<Tool*> ToolRegistry::getAllTools() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Tool*> result;
    result.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        if (tool->isEnabled() && !tool->isHidden()) {
            result.push_back(tool.get());
        }
    }
    return result;
}

std::vector<Tool*> ToolRegistry::getToolsByCategory(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Tool*> result;
    for (const auto& [name, tool] : tools_) {
        if (tool->isEnabled() && !tool->isHidden() && tool->getCategory() == category) {
            result.push_back(tool.get());
        }
    }
    return result;
}

std::vector<std::string> ToolRegistry::getToolNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        if (tool->isEnabled()) names.push_back(name);
    }
    return names;
}

bool ToolRegistry::hasTool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tools_.count(name)) return true;
    auto ait = aliases_.find(name);
    return ait != aliases_.end() && tools_.count(ait->second);
}

nlohmann::json ToolRegistry::toApiToolDefinitions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json defs = nlohmann::json::array();

    for (const auto& [name, tool] : tools_) {
        if (!tool->isEnabled() || tool->isHidden()) continue;

        nlohmann::json def;
        def["name"] = tool->getName();
        def["description"] = tool->getDescription();
        def["input_schema"] = tool->getInputSchema();
        defs.push_back(std::move(def));
    }
    return defs;
}

std::string ToolRegistry::toSystemPromptDescription() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string desc;

    for (const auto& [name, tool] : tools_) {
        if (!tool->isEnabled() || tool->isHidden()) continue;
        desc += "- " + tool->getName() + ": " + tool->getDescription() + "\n";
    }
    return desc;
}

} // namespace closecrab
