#pragma once

#include "Tool.h"
#include <map>
#include <memory>
#include <vector>
#include <string>
#include <mutex>

namespace closecrab {

class ToolRegistry {
public:
    static ToolRegistry& getInstance();

    void registerTool(std::unique_ptr<Tool> tool);
    Tool* getTool(const std::string& name) const;
    std::vector<Tool*> getAllTools() const;
    std::vector<Tool*> getToolsByCategory(const std::string& category) const;
    std::vector<std::string> getToolNames() const;
    bool hasTool(const std::string& name) const;

    // Generate tool definitions for API (Anthropic format)
    nlohmann::json toApiToolDefinitions() const;

    // Generate tool list for system prompt
    std::string toSystemPromptDescription() const;

private:
    ToolRegistry() = default;
    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Tool>> tools_;
    std::map<std::string, std::string> aliases_;  // alias -> canonical name
};

} // namespace closecrab
