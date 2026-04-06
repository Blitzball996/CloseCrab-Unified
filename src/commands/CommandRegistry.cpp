#include "CommandRegistry.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>

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
    std::ostringstream oss;
    oss << "Available commands:\n";

    // Group by category (first letter for now)
    for (const auto& [name, cmd] : commands_) {
        if (!cmd->isEnabled() || cmd->isHidden()) continue;
        oss << "  /" << name;
        auto aliases = cmd->getAliases();
        if (!aliases.empty()) {
            oss << " (";
            for (size_t i = 0; i < aliases.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "/" << aliases[i];
            }
            oss << ")";
        }
        oss << " - " << cmd->getDescription() << "\n";
    }
    return oss.str();
}

} // namespace closecrab
