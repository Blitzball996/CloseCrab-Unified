#include "CommandRegistry.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <set>

namespace closecrab {

CommandRegistry& CommandRegistry::getInstance() {
    static CommandRegistry instance;
    return instance;
}

void CommandRegistry::registerCommand(std::unique_ptr<Command> cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string name = cmd->getName();
    auto aliases = cmd->getAliases();

    spdlog::debug("Registering command: /{}", name);
    commands_[name] = std::move(cmd);

    for (const auto& alias : aliases) {
        aliases_[alias] = name;
    }
}

Command* CommandRegistry::findCommand(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = commands_.find(name);
    if (it != commands_.end()) return it->second.get();

    auto ait = aliases_.find(name);
    if (ait != aliases_.end()) {
        it = commands_.find(ait->second);
        if (it != commands_.end()) return it->second.get();
    }
    return nullptr;
}

std::vector<Command*> CommandRegistry::getAllCommands() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Command*> result;
    for (const auto& [name, cmd] : commands_) {
        if (cmd->isEnabled() && !cmd->isHidden()) {
            result.push_back(cmd.get());
        }
    }
    return result;
}

std::vector<std::string> CommandRegistry::getCommandNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, cmd] : commands_) {
        if (cmd->isEnabled()) names.push_back(name);
    }
    return names;
}

bool CommandRegistry::hasCommand(const std::string& name) const {
    return findCommand(name) != nullptr;
}

bool CommandRegistry::executeCommand(const std::string& name, const std::string& args,
                                      CommandContext& ctx, CommandResult& result) {
    Command* cmd = findCommand(name);
    if (!cmd) return false;

    try {
        result = cmd->execute(args, ctx);
    } catch (const std::exception& e) {
        result = CommandResult::fail(std::string("Command error: ") + e.what());
    }
    return true;
}

bool CommandRegistry::isCommand(const std::string& input) {
    if (input.empty()) return false;
    return input[0] == '/';
}

std::pair<std::string, std::string> CommandRegistry::parseCommand(const std::string& input) {
    if (!isCommand(input)) return {"", input};

    // Skip the '/'
    std::string rest = input.substr(1);

    // Split on first space
    size_t space = rest.find(' ');
    if (space == std::string::npos) {
        return {rest, ""};
    }

    std::string name = rest.substr(0, space);
    std::string args = rest.substr(space + 1);
    // Trim leading spaces from args
    size_t start = args.find_first_not_of(' ');
    if (start != std::string::npos) args = args.substr(start);
    else args.clear();

    return {name, args};
}

std::string CommandRegistry::getHelpText() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Categorize commands
    struct Category {
        std::string name;
        std::vector<std::string> keywords;
    };
    std::vector<Category> categories = {
        {"Session",  {"help", "quit", "exit", "clear", "new", "session", "history", "export",
                      "compact", "context", "status", "version", "env"}},
        {"Model",    {"model", "cost", "fast", "thinking", "brief", "permissions", "provider", "api"}},
        {"Git",      {"commit", "diff", "branch", "log", "push", "pull", "stash", "pr", "review"}},
        {"Tools",    {"tools", "skills", "plugin", "mcp", "hooks", "agents", "tasks", "audit"}},
        {"Advanced", {"rag", "ssd", "sandbox", "plan", "doctor", "coordinator", "vim",
                      "add-dir", "files", "reload", "memory", "share"}}
    };

    std::ostringstream oss;
    oss << "\033[1mAvailable commands:\033[0m\n\n";

    std::set<std::string> shown;
    for (const auto& cat : categories) {
        std::ostringstream catOss;
        for (const auto& kw : cat.keywords) {
            auto it = commands_.find(kw);
            if (it == commands_.end() || !it->second->isEnabled() || it->second->isHidden()) continue;
            if (shown.count(kw)) continue;
            shown.insert(kw);

            catOss << "  \033[33m/" << kw << "\033[0m";
            auto aliases = it->second->getAliases();
            if (!aliases.empty()) {
                catOss << " \033[90m(";
                for (size_t i = 0; i < aliases.size(); i++) {
                    if (i > 0) catOss << ",";
                    catOss << "/" << aliases[i];
                }
                catOss << ")\033[0m";
            }
            catOss << "  " << it->second->getDescription() << "\n";
        }
        std::string catStr = catOss.str();
        if (!catStr.empty()) {
            oss << "  \033[1;36m" << cat.name << "\033[0m\n" << catStr << "\n";
        }
    }

    // Show uncategorized commands
    std::ostringstream otherOss;
    for (const auto& [name, cmd] : commands_) {
        if (!cmd->isEnabled() || cmd->isHidden() || shown.count(name)) continue;
        otherOss << "  \033[33m/" << name << "\033[0m  " << cmd->getDescription() << "\n";
    }
    std::string otherStr = otherOss.str();
    if (!otherStr.empty()) {
        oss << "  \033[1;36mOther\033[0m\n" << otherStr << "\n";
    }

    oss << "  \033[90mTip: ! prefix runs shell commands (e.g. !git status)\033[0m\n";
    return oss.str();
}

} // namespace closecrab
