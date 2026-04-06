#pragma once

#include "Command.h"
#include <map>
#include <memory>
#include <vector>
#include <mutex>

namespace closecrab {

class CommandRegistry {
public:
    static CommandRegistry& getInstance();

    void registerCommand(std::unique_ptr<Command> cmd);
    Command* findCommand(const std::string& name) const;
    std::vector<Command*> getAllCommands() const;
    std::vector<std::string> getCommandNames() const;
    bool hasCommand(const std::string& name) const;

    // Execute a command by name. Returns false if command not found.
    bool executeCommand(const std::string& name, const std::string& args, CommandContext& ctx,
                        CommandResult& result);

    // Parse "/command args" input. Returns true if it's a command.
    static bool isCommand(const std::string& input);
    static std::pair<std::string, std::string> parseCommand(const std::string& input);

    // Generate help text
    std::string getHelpText() const;

private:
    CommandRegistry() = default;

    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Command>> commands_;
    std::map<std::string, std::string> aliases_;  // alias -> canonical name
};

} // namespace closecrab
