#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace closecrab {

// Forward declarations
struct AppState;
class QueryEngine;
class ToolRegistry;
class APIClient;

// ============================================================
// Command result
// ============================================================

struct CommandResult {
    bool success = true;
    std::string output;
    bool shouldContinue = true;  // false = exit program

    static CommandResult ok(const std::string& out = "") { return {true, out, true}; }
    static CommandResult fail(const std::string& err) { return {false, err, true}; }
    static CommandResult quit() { return {true, "", false}; }
};

// ============================================================
// Command context (passed to every command handler)
// ============================================================

struct CommandContext {
    QueryEngine* queryEngine = nullptr;
    AppState* appState = nullptr;
    ToolRegistry* toolRegistry = nullptr;
    APIClient* apiClient = nullptr;
    std::string cwd;

    // Output function (print to terminal)
    std::function<void(const std::string&)> print;
};

// ============================================================
// Command base class (对标 JackProAi Command interface)
// ============================================================

class Command {
public:
    virtual ~Command() = default;

    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;
    virtual std::vector<std::string> getAliases() const { return {}; }
    virtual bool isHidden() const { return false; }
    virtual bool isEnabled() const { return true; }

    virtual CommandResult execute(const std::string& args, CommandContext& ctx) = 0;
};

// ============================================================
// Simple command: wraps a lambda for quick command creation
// ============================================================

class SimpleCommand : public Command {
public:
    using Handler = std::function<CommandResult(const std::string& args, CommandContext& ctx)>;

    SimpleCommand(const std::string& name, const std::string& desc, Handler handler,
                  std::vector<std::string> aliases = {}, bool hidden = false)
        : name_(name), desc_(desc), handler_(std::move(handler)),
          aliases_(std::move(aliases)), hidden_(hidden) {}

    std::string getName() const override { return name_; }
    std::string getDescription() const override { return desc_; }
    std::vector<std::string> getAliases() const override { return aliases_; }
    bool isHidden() const override { return hidden_; }
    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        return handler_(args, ctx);
    }

private:
    std::string name_;
    std::string desc_;
    Handler handler_;
    std::vector<std::string> aliases_;
    bool hidden_;
};

} // namespace closecrab
