#pragma once

#include <string>
#include <set>
#include <filesystem>
#include <algorithm>

namespace closecrab {

// Security: Block UNC paths to prevent NTLM credential leaks
// JackProAi FileReadTool.ts:463-467
inline bool isUncPath(const std::string& path) {
    return path.size() >= 2 && (path.substr(0, 2) == "\\\\" || path.substr(0, 2) == "//");
}

// Security: Block device files that would hang (infinite output or blocking input)
// JackProAi FileReadTool.ts:98-128
inline bool isBlockedDevicePath(const std::string& path) {
    static const std::set<std::string> blocked = {
     // Infinite output
        "/dev/zero", "/dev/random", "/dev/urandom", "/dev/full",
        // Blocks waiting for input
        "/dev/stdin", "/dev/tty", "/dev/console",
        // Nonsensical
        "/dev/stdout", "/dev/stderr",
        // fd aliases
        "/dev/fd/0", "/dev/fd/1", "/dev/fd/2"
    };

    if (blocked.count(path) > 0) return true;

    // /proc/self/fd/0-2 and /proc/<pid>/fd/0-2
    if (path.find("/proc/") == 0 &&
        (path.find("/fd/0") != std::string::npos ||
         path.find("/fd/1") != std::string::npos ||
         path.find("/fd/2") != std::string::npos)) {
        return true;
    }

    return false;
}

// Find similar file in same directory (typo correction)
// JackProAi utils/file.ts:findSimilarFile
inline std::string findSimilarFile(const std::filesystem::path& missingPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(missingPath.parent_path())) return "";

    std::string target = missingPath.filename().string();
    std::transform(target.begin(), target.end(), target.begin(), ::tolower);

    std::string bestMatch;
    int bestScore = INT_MAX;

    try {
        for (const auto& entry : fs::directory_iterator(missingPath.parent_path())) {
            if (!entry.is_regular_file()) continue;

            std::string candidate = entry.path().filename().string();
            std::string candidateLower = candidate;
            std::transform(candidateLower.begin(), candidateLower.end(),
               candidateLower.begin(), ::tolower);

            // Simple Levenshtein distance (simplified for performance)
            if (candidateLower == target) continue; // Exact match, not similar

          // Check if one is substring of other
            if (candidateLower.find(target) != std::string::npos ||
             target.find(candidateLower) != std::string::npos) {
                if (candidate.size() < bestMatch.size() || bestMatch.empty()) {
                    bestMatch = candidate;
             }
            }
        }
    } catch (...) {}

    return bestMatch;
}

// Suggest path under current working directory
// JackProAi utils/file.ts:suggestPathUnderCwd
inline std::string suggestPathUnderCwd(const std::filesystem::path& path,
                       const std::string& cwd) {
    namespace fs = std::filesystem;

    // If path is absolute and exists under cwd, suggest the relative version
    if (path.is_absolute()) {
        fs::path cwdPath(cwd);
        std::string pathStr = path.string();
        std::string cwdStr = cwdPath.string();

        // Check if file exists as relative path from cwd
        if (pathStr.size() > cwdStr.size()) {
       fs::path filename = path.filename();
            fs::path relativePath = cwdPath / filename;

            if (fs::exists(relativePath)) {
                return filename.string();
            }
        }
    }

    return "";
}

// macOS screenshot path resolution (U+202F thin space)
// JackProAi FileReadTool.ts:147-159
inline std::string getAlternateScreenshotPath(const std::string& path) {
    // Check if filename matches screenshot pattern with AM/PM
    size_t pos = path.rfind(" AM.png");
    if (pos == std::string::npos) pos = path.rfind(" PM.png");
    if (pos == std::string::npos) {
        // Try thin space (U+202F = 0xE2 0x80 0xAF in UTF-8)
        pos = path.find("\xE2\x80\xAF" "AM.png");
        if (pos == std::string::npos) pos = path.find("\xE2\x80\xAF" "PM.png");
    }

    if (pos == std::string::npos) return "";

    // Try alternate space character
    std::string alt = path;
    if (path[pos] == ' ') {
        // Replace regular space with thin space
        alt.replace(pos, 1, "\xE2\x80\xAF");
    } else {
        // Replace thin space with regular space
        alt.replace(pos, 3, " ");
    }

    return alt;
}

} // namespace closecrab
