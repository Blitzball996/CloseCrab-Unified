#pragma once

#include "Command.h"
#include <cstdio>
#include <array>
#include <memory>
#include <sstream>

namespace closecrab {

// Helper: run shell command and capture output
inline std::string shellExec(const std::string& cmd) {
    std::string result;
#ifdef _WIN32
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(("cmd /c \"" + cmd + "\" 2>&1").c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen((cmd + " 2>&1").c_str(), "r"), pclose);
#endif
    if (!pipe) return "[exec failed]";
    std::array<char, 4096> buf;
    while (fgets(buf.data(), buf.size(), pipe.get())) result += buf.data();
    return result;
}

// /commit
class CommitCommand : public Command {
public:
    std::string getName() const override { return "commit"; }
    std::string getDescription() const override { return "Create a git commit with auto-generated message"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        // Show status
        std::string status = shellExec("git status --short");
        if (status.empty()) {
            ctx.print("Nothing to commit, working tree clean.\n");
            return CommandResult::ok();
        }
        ctx.print("Changes:\n" + status + "\n");

        // If args provided, use as commit message
        if (!args.empty()) {
            std::string out = shellExec("git add -A && git commit -m \"" + args + "\"");
            ctx.print(out);
            return CommandResult::ok();
        }

        // Otherwise ask for message
        ctx.print("Commit message (or empty to cancel): ");
        std::string msg;
        std::getline(std::cin, msg);
        if (msg.empty()) return CommandResult::ok("Cancelled.\n");

        std::string out = shellExec("git add -A && git commit -m \"" + msg + "\"");
        ctx.print(out);
        return CommandResult::ok();
    }
};

// /diff
class DiffCommand : public Command {
public:
    std::string getName() const override { return "diff"; }
    std::string getDescription() const override { return "Show git diff (staged and unstaged)"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string diff = shellExec("git diff" + (args.empty() ? "" : " " + args));
        std::string staged = shellExec("git diff --cached");
        std::string out;
        if (!staged.empty()) out += "=== Staged ===\n" + staged + "\n";
        if (!diff.empty()) out += "=== Unstaged ===\n" + diff + "\n";
        if (out.empty()) out = "No changes.\n";
        ctx.print(out);
        return CommandResult::ok();
    }
};

// /branch
class BranchCommand : public Command {
public:
    std::string getName() const override { return "branch"; }
    std::string getDescription() const override { return "List, create, or switch git branches"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        if (args.empty()) {
            ctx.print(shellExec("git branch -a"));
        } else if (args.substr(0, 2) == "-d") {
            ctx.print(shellExec("git branch " + args));
        } else {
            // Create and switch
            ctx.print(shellExec("git checkout -b " + args));
        }
        return CommandResult::ok();
    }
};

// /log
class LogCommand : public Command {
public:
    std::string getName() const override { return "log"; }
    std::string getDescription() const override { return "Show recent git log"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string count = args.empty() ? "10" : args;
        ctx.print(shellExec("git log --oneline -n " + count));
        return CommandResult::ok();
    }
};

// /push
class PushCommand : public Command {
public:
    std::string getName() const override { return "push"; }
    std::string getDescription() const override { return "Push to remote repository"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.print("Pushing...\n");
        std::string cmd = "git push" + (args.empty() ? "" : " " + args);
        ctx.print(shellExec(cmd));
        return CommandResult::ok();
    }
};

// /pull
class PullCommand : public Command {
public:
    std::string getName() const override { return "pull"; }
    std::string getDescription() const override { return "Pull from remote repository"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        ctx.print(shellExec("git pull" + (args.empty() ? "" : " " + args)));
        return CommandResult::ok();
    }
};

// /stash
class StashCommand : public Command {
public:
    std::string getName() const override { return "stash"; }
    std::string getDescription() const override { return "Git stash operations"; }

    CommandResult execute(const std::string& args, CommandContext& ctx) override {
        std::string cmd = "git stash" + (args.empty() ? "" : " " + args);
        ctx.print(shellExec(cmd));
        return CommandResult::ok();
    }
};

} // namespace closecrab
