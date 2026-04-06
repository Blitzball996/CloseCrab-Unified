#pragma once

#include <string>
#include <vector>
#include <cstdio>
#include <array>
#include <memory>

namespace closecrab {

// Git operations wrapper — calls git CLI
class GitManager {
public:
    static std::string exec(const std::string& cmd) {
        std::string result;
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(
            _popen(("cmd /c \"git " + cmd + "\" 2>&1").c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(
            popen(("git " + cmd + " 2>&1").c_str(), "r"), pclose);
#endif
        if (!pipe) return "";
        std::array<char, 4096> buf;
        while (fgets(buf.data(), buf.size(), pipe.get())) result += buf.data();
        return result;
    }

    static std::string status() { return exec("status --short"); }
    static std::string diff(bool staged = false) {
        return exec(staged ? "diff --cached" : "diff");
    }
    static std::string log(int count = 10) {
        return exec("log --oneline -n " + std::to_string(count));
    }
    static std::string commit(const std::string& message, const std::vector<std::string>& files = {}) {
        if (!files.empty()) {
            std::string fileList;
            for (const auto& f : files) fileList += " \"" + f + "\"";
            exec("add" + fileList);
        }
        return exec("commit -m \"" + message + "\"");
    }
    static std::string push(const std::string& remote = "", const std::string& branch = "") {
        std::string cmd = "push";
        if (!remote.empty()) cmd += " " + remote;
        if (!branch.empty()) cmd += " " + branch;
        return exec(cmd);
    }
    static std::string branch(const std::string& name = "") {
        return name.empty() ? exec("branch -a") : exec("checkout -b " + name);
    }
    static std::string worktreeAdd(const std::string& path, const std::string& branch) {
        return exec("worktree add \"" + path + "\" -b " + branch + " HEAD");
    }
    static std::string worktreeRemove(const std::string& path, bool force = false) {
        return exec("worktree remove \"" + path + "\"" + (force ? " --force" : ""));
    }
    static std::string currentBranch() {
        std::string b = exec("rev-parse --abbrev-ref HEAD");
        if (!b.empty() && b.back() == '\n') b.pop_back();
        return b;
    }
    static bool isRepo() {
        std::string r = exec("rev-parse --is-inside-work-tree");
        return r.find("true") != std::string::npos;
    }
};

} // namespace closecrab
