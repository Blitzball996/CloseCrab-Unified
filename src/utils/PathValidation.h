#pragma once

#include <string>
#include <regex>
#include <cstdlib>

namespace closecrab {

// §5: Detect dangerous removal paths (JackProAi isDangerousRemovalPath pattern)
//
// Returns true for paths that should never be removed without explicit confirmation:
// - Wildcard "*" or paths ending in "/*"
// - Root directory "/"
// - Windows drive root: C:\, D:\, etc.
// - Windows drive direct children: C:\Windows, C:\Users, etc.
// - User home directory
// - Direct children of POSIX root: /usr, /tmp, /etc, etc.
inline bool isDangerousRemovalPath(const std::string& resolvedPath) {
    if (resolvedPath.empty()) return false;

    // Normalize: collapse backslash/forward-slash runs to single forward slash
    std::string normalized = resolvedPath;
    {
        std::string tmp;
        tmp.reserve(normalized.size());
        bool lastWasSlash = false;
        for (char c : normalized) {
            if (c == '\\' || c == '/') {
                if (!lastWasSlash) tmp.push_back('/');
                lastWasSlash = true;
            } else {
                tmp.push_back(c);
                lastWasSlash = false;
            }
        }
        normalized = std::move(tmp);
    }

    // Wildcard
    if (normalized == "*") return true;
    if (normalized.size() >= 2 && normalized.substr(normalized.size() - 2) == "/*") return true;

    // Strip trailing slash for further checks (but keep "/" as-is)
    std::string trimmed = normalized;
    if (trimmed.size() > 1 && trimmed.back() == '/') trimmed.pop_back();

    // POSIX root
    if (trimmed == "/") return true;

    // Windows drive root: C:, C:/, D:, etc.
    if (trimmed.size() == 2 && std::isalpha((unsigned char)trimmed[0]) && trimmed[1] == ':') return true;
    if (trimmed.size() == 3 && std::isalpha((unsigned char)trimmed[0]) && trimmed[1] == ':' && trimmed[2] == '/') return true;

    // Windows drive direct children: C:/Windows, C:/Users, etc.
    if (trimmed.size() > 3 && std::isalpha((unsigned char)trimmed[0]) && trimmed[1] == ':' && trimmed[2] == '/') {
        // Check no further '/' after the drive
        std::string rest = trimmed.substr(3);
        if (rest.find('/') == std::string::npos) return true;
    }

    // Home directory
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (home && home[0]) {
        std::string normHome;
        for (char c : std::string(home)) normHome.push_back(c == '\\' ? '/' : c);
        if (!normHome.empty() && normHome.back() == '/') normHome.pop_back();
        if (trimmed == normHome) return true;
    }

    // POSIX root direct children: /usr, /tmp, /etc — parent dir is "/"
    if (trimmed.size() > 1 && trimmed[0] == '/') {
        std::string rest = trimmed.substr(1);
        if (rest.find('/') == std::string::npos) return true;
    }

    return false;
}

// §5: Match dangerous command patterns (JackProAi destructiveCommandWarning)
// Returns a warning string if matched, or empty string if safe.
inline std::string getDestructiveCommandWarning(const std::string& cmd) {
    static const std::pair<std::regex, std::string> patterns[] = {
        // File deletion
        {std::regex(R"((^|[;&|\n]\s*)rm\s+-[a-zA-Z]*[rR][a-zA-Z]*f|(^|[;&|\n]\s*)rm\s+-[a-zA-Z]*f[a-zA-Z]*[rR])"),
         "may recursively force-remove files"},
        {std::regex(R"((^|[;&|\n]\s*)rm\s+-[a-zA-Z]*[rR])"),
         "may recursively remove files"},
        {std::regex(R"((^|[;&|\n]\s*)rm\s+-[a-zA-Z]*f)"),
         "may force-remove files"},
        {std::regex(R"((^|[;&|\n]\s*)rm\s+)"),
         "may remove files"},
        {std::regex(R"((^|[;&|\n]\s*)rmdir\s+)"),
         "may remove a directory"},
        {std::regex(R"((^|[;&|\n]\s*)del\s+)", std::regex::icase),
         "may delete files (Windows)"},
        // Git destructive
        {std::regex(R"(\bgit\s+reset\s+--hard\b)"),
         "may discard uncommitted changes"},
        {std::regex(R"(\bgit\s+push\b[^;&|\n]*[ \t](--force|--force-with-lease|-f)\b)"),
         "may overwrite remote history"},
        {std::regex(R"(\bgit\s+checkout\s+(--\s+)?\.[ \t]*($|[;&|\n]))"),
         "may discard all working tree changes"},
        {std::regex(R"(\bgit\s+clean\b[^;&|\n]*-[a-zA-Z]*f)"),
         "may permanently delete untracked files"},
        // SQL / Infra
        {std::regex(R"(\b(DROP|TRUNCATE)\s+(TABLE|DATABASE|SCHEMA)\b)", std::regex::icase),
         "may drop or truncate database objects"},
        {std::regex(R"(\bkubectl\s+delete\b)"),
         "may delete Kubernetes resources"},
        {std::regex(R"(\bterraform\s+destroy\b)"),
         "may destroy Terraform infrastructure"},
    };
    for (const auto& [regex, warning] : patterns) {
        if (std::regex_search(cmd, regex)) return warning;
    }
    return "";
}

// Extract the first path-like argument from an rm/rmdir/del command.
// Used by checkPermissions to feed isDangerousRemovalPath.
// Naive: handles `rm path`, `rm -rf path`, `rm "quoted path"`. Does not handle
// shell expansion / multiple paths / pipelines.
inline std::vector<std::string> extractRmTargets(const std::string& cmd) {
    std::vector<std::string> paths;
    // Quick check
    std::regex cmdRe(R"((^|[;&|\n]\s*)(rm|rmdir|del)\s+(.+?)(?:[;&|\n]|$))",
                     std::regex::icase);
    auto begin = std::sregex_iterator(cmd.begin(), cmd.end(), cmdRe);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string args = (*it)[3].str();
        // Tokenize, skip flags
        std::string token;
        bool inQuote = false;
        char quoteChar = 0;
        auto flush = [&]() {
            if (!token.empty() && token[0] != '-') paths.push_back(token);
            token.clear();
        };
        for (char c : args) {
            if (inQuote) {
                if (c == quoteChar) inQuote = false;
                else token.push_back(c);
            } else if (c == '"' || c == '\'') {
                inQuote = true; quoteChar = c;
            } else if (c == ' ' || c == '\t') {
                flush();
            } else {
                token.push_back(c);
            }
        }
        flush();
    }
    return paths;
}

} // namespace closecrab
