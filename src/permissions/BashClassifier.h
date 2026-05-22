#pragma once

#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <set>

namespace closecrab {

enum class CommandRisk {
    SAFE,       // Read-only, auto-approve
    WRITE,      // Creates/modifies files, approve in auto mode
    DANGEROUS,  // Destructive, always ask
    UNKNOWN     // Ask in default mode
};

class BashClassifier {
public:
    static CommandRisk classify(const std::string& cmd) {
        if (cmd.empty()) return CommandRisk::UNKNOWN;

        // Check dangerous patterns first (highest priority)
        if (hasDangerousPattern(cmd)) return CommandRisk::DANGEROUS;

        // Split on pipes and check each segment
        auto segments = splitPipes(cmd);
        CommandRisk worst = CommandRisk::SAFE;

        for (const auto& seg : segments) {
            std::string base = extractBaseCommand(seg);
            CommandRisk r = classifyBase(base);
            if (r == CommandRisk::DANGEROUS) return CommandRisk::DANGEROUS;
            if (r > worst) worst = r;
        }
        return worst;
    }

private:
    static std::string extractBaseCommand(const std::string& segment) {
        std::string s = segment;
        // Strip leading whitespace
        size_t start = s.find_first_not_of(" \t");
        if (start == std::string::npos) return "";
        s = s.substr(start);
        // Strip sudo/doas prefix
        if (s.substr(0, 5) == "sudo " || s.substr(0, 5) == "doas ") {
            s = s.substr(5);
            start = s.find_first_not_of(" \t");
            if (start != std::string::npos) s = s.substr(start);
        }
        // Extract first word
        size_t end = s.find_first_of(" \t\n;");
        return end != std::string::npos ? s.substr(0, end) : s;
    }

    static std::vector<std::string> splitPipes(const std::string& cmd) {
        std::vector<std::string> parts;
        std::string current;
        bool inQuote = false;
        char quoteChar = 0;
        for (size_t i = 0; i < cmd.size(); i++) {
            char c = cmd[i];
            if (!inQuote && (c == '\'' || c == '"')) { inQuote = true; quoteChar = c; }
            else if (inQuote && c == quoteChar) { inQuote = false; }
            else if (!inQuote && (c == '|' || c == ';' || c == '&')) {
                if (!current.empty()) parts.push_back(current);
                current.clear();
                if (c == '&' && i+1 < cmd.size() && cmd[i+1] == '&') i++;
                continue;
            }
            current += c;
        }
        if (!current.empty()) parts.push_back(current);
        return parts;
    }

    static CommandRisk classifyBase(const std::string& base) {
        static const std::set<std::string> safe = {
            "ls", "cat", "head", "tail", "echo", "pwd", "whoami", "date", "find",
            "grep", "rg", "which", "where", "type", "wc", "sort", "uniq", "diff",
            "file", "stat", "env", "printenv", "hostname", "uname", "df", "du",
            "free", "top", "ps", "id", "groups", "test", "true", "false", "seq",
            "basename", "dirname", "realpath", "readlink", "tee"
        };
        static const std::set<std::string> safeGit = {
            "status", "log", "diff", "branch", "show", "tag", "remote", "stash"
        };
        static const std::set<std::string> write = {
            "mkdir", "touch", "cp", "mv", "tee", "npm", "pip", "pip3", "cargo",
            "cmake", "make", "python", "python3", "node", "go", "javac", "gcc",
            "g++", "clang", "rustc", "dotnet", "yarn", "pnpm", "composer"
        };
        static const std::set<std::string> dangerous = {
            "rm", "rmdir", "kill", "pkill", "killall", "shutdown", "reboot",
            "poweroff", "halt", "format", "fdisk", "dd", "mkfs", "shred",
            "iptables", "ufw"
        };

        if (base == "git") return CommandRisk::WRITE;
        if (safe.count(base)) return CommandRisk::SAFE;
        if (write.count(base)) return CommandRisk::WRITE;
        if (dangerous.count(base)) return CommandRisk::DANGEROUS;
        return CommandRisk::UNKNOWN;
    }

    static bool hasDangerousPattern(const std::string& cmd) {
        static const std::vector<std::string> patterns = {
            "rm\\s+(-[a-zA-Z]*f[a-zA-Z]*\\s+)?(/|~|\\*)",
            "rm\\s+-rf\\s+/",
            "--no-preserve-root",
            ":\\(\\)\\{\\s*:\\|:\\&\\s*\\}\\;:",
            ">(\\s*/dev/sd|\\s*/dev/null)",
            "chmod\\s+(-R\\s+)?777\\s+/",
            "git\\s+push.*--force",
            "git\\s+reset\\s+--hard",
            "git\\s+clean\\s+-[a-z]*f",
            "DROP\\s+(TABLE|DATABASE)",
            "DELETE\\s+FROM.*WHERE\\s+1",
            "TRUNCATE\\s+TABLE",
            "curl.*\\|\\s*(ba)?sh",
            "wget.*\\|\\s*(ba)?sh"
        };
        for (const auto& p : patterns) {
            try {
                if (std::regex_search(cmd, std::regex(p, std::regex::icase))) {
                    return true;
                }
            } catch (...) {}
        }
        return false;
    }
};

} // namespace closecrab
