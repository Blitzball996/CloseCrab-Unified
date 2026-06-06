#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <regex>
#include <cstring>

namespace closecrab {

// Permission rule for file operations
struct PermissionRule {
    std::string pattern;     // Wildcard pattern (e.g., "**/secrets/**", "*.key")
    std::string action;      // "deny" or "allow"
    std::string operation;   // "read", "write", or "all"
};

class PermissionManager {
public:
    static PermissionManager& getInstance() {
        static PermissionManager instance;
        return instance;
    }

    // Load permissions from .closecrab/permissions.json
    void loadPermissions(const std::filesystem::path& projectRoot) {
        namespace fs = std::filesystem;

        fs::path configPath = projectRoot / ".closecrab" / "permissions.json";
        if (!fs::exists(configPath)) {
            // Use default permissions if no config file
            loadDefaultPermissions();
            return;
        }

        try {
            std::ifstream file(configPath);
            nlohmann::json config;
         file >> config;

            // Parse read deny rules
            if (config.contains("read") && config["read"].contains("deny")) {
             for (const auto& pattern : config["read"]["deny"]) {
            readDenyPatterns_.push_back(pattern.get<std::string>());
                }
            }

          // Parse write deny rules
            if (config.contains("write") && config["write"].contains("deny")) {
        for (const auto& pattern : config["write"]["deny"]) {
                  writeDenyPatterns_.push_back(pattern.get<std::string>());
                }
            }
        } catch (const std::exception& e) {
     // Fallback to defaults on parse error
            loadDefaultPermissions();
        }
    }

    // Check if a path is denied for reading
    bool isReadDenied(const std::string& path) const {
        return matchesAnyPattern(path, readDenyPatterns_);
    }

    // Check if a path is denied for writing
    bool isWriteDenied(const std::string& path) const {
     return matchesAnyPattern(path, writeDenyPatterns_);
    }

    // Get human-readable error message
    std::string getDenyMessage(const std::string& path, const std::string& operation) const {
        return "File is in a directory that is denied by your permission settings.\n"
             "Path: " + path + "\n"
           "Operation: " + operation + "\n"
             "Check .closecrab/permissions.json to modify these rules.";
    }

private:
    std::vector<std::string> readDenyPatterns_;
    std::vector<std::string> writeDenyPatterns_;

    PermissionManager() {
        loadDefaultPermissions();
    }

    // Default security rules (hardcoded)
    void loadDefaultPermissions() {
        // Sensitive directories
        readDenyPatterns_ = {
         "**/.git/objects/**",      // Git binary objects
            "**/.git/lfs/**",          // Git LFS objects
            "**/node_modules/**",      // npm dependencies (too large)
            "**/.cache/**",            // Build caches
         "**/__pycache__/**",       // Python bytecode
            "**/venv/**",              // Python virtual envs
            "**/vendor/**",            // Dependency dirs

            // Sensitive files
         "**/*.key",                // Private keys
            "**/*.pem",                // Certificates
         "**/*.p12",              // PKCS12 keystores
            "**/*.pfx",
            "**/*.keystore",
          "**/id_rsa",           // SSH private keys
         "**/id_dsa",
         "**/id_ed25519",
            "**/.env",                 // Environment variables
            "**/.env.local",
          "**/.env.production",
       "**/secrets.json",
            "**/credentials.json",

          // System files
         "**/Thumbs.db",            // Windows thumbnail cache
       "**/.DS_Store",            // macOS metadata
            "**/desktop.ini"
        };

        writeDenyPatterns_ = {
            "**/.git/**",           // Protect Git directory
            "**/node_modules/**",      // Don't write to dependencies
            "**/.cache/**",
       "**/venv/**",
         "**/vendor/**"
        };
    }

    // Match path against wildcard patterns
    bool matchesAnyPattern(const std::string& path, const std::vector<std::string>& patterns) const {
        for (const auto& pattern : patterns) {
            if (matchWildcard(path, pattern)) {
                return true;
            }
        }
        return false;
  }

    // Translate a glob (with *, **, ?) into an anchored regex.
    //   **/  -> (?:.*/)?   (zero-or-more leading dirs)
    //   **   -> .*         (any chars, including '/')
    //   *    -> [^/]*      (any chars within one path segment)
    //   ?    -> [^/]       (one char within a segment)
    // Regex metacharacters are escaped. Matching is case-insensitive on Windows.
    static std::string globToRegex(const std::string& glob) {
        std::string re = "^";
        for (size_t i = 0; i < glob.size(); ++i) {
            char c = glob[i];
            if (c == '*') {
                if (i + 1 < glob.size() && glob[i + 1] == '*') {
                    if (i + 2 < glob.size() && glob[i + 2] == '/') {
                        re += "(?:.*/)?";   // **/  -> optional leading dirs
                        i += 2;
                    } else {
                        re += ".*";         // **   -> across segments
                        i += 1;
                    }
                } else {
                    re += "[^/]*";          // *    -> within a segment
                }
            } else if (c == '?') {
                re += "[^/]";
            } else if (std::strchr(".^$+{}()|[]\\", c)) {
                re += '\\';
                re += c;
            } else {
                re += c;
            }
        }
        re += "$";
        return re;
    }

    // Glob match using proper segment-aware semantics (replaces the old crude
    // substring matcher that couldn't handle **/dir/** or *.ext correctly).
    bool matchWildcard(const std::string& path, const std::string& pattern) const {
        std::string normPath = path;
        std::string normPattern = pattern;
        std::replace(normPath.begin(), normPath.end(), '\\', '/');
        std::replace(normPattern.begin(), normPattern.end(), '\\', '/');
#ifdef _WIN32
        std::transform(normPath.begin(), normPath.end(), normPath.begin(), ::tolower);
        std::transform(normPattern.begin(), normPattern.end(), normPattern.begin(), ::tolower);
#endif
        try {
            std::regex re(globToRegex(normPattern),
                          std::regex::ECMAScript | std::regex::optimize);
            return std::regex_match(normPath, re);
        } catch (...) {
            // Malformed pattern — fall back to exact match rather than throw.
            return normPath == normPattern;
        }
    }
};

} // namespace closecrab
