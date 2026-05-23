#pragma once
#include <string>
#include <filesystem>

namespace closecrab {

struct ProjectInfo {
    std::string language;    // "C++", "Python", "JavaScript", etc.
    std::string framework;   // "React", "Flask", "CMake", etc.
    std::string buildCmd;    // "cmake --build .", "npm run build", etc.
    std::string testCmd;     // "ctest", "npm test", "pytest", etc.
};

class ProjectDetector {
public:
    static ProjectInfo detect(const std::string& projectRoot) {
        namespace fs = std::filesystem;
        ProjectInfo info;

        if (fs::exists(fs::path(projectRoot) / "CMakeLists.txt")) {
            info.language = "C++"; info.framework = "CMake";
            info.buildCmd = "cmake --build build"; info.testCmd = "ctest --test-dir build";
        } else if (fs::exists(fs::path(projectRoot) / "package.json")) {
            info.language = "JavaScript/TypeScript"; info.framework = "Node.js";
            info.buildCmd = "npm run build"; info.testCmd = "npm test";
        } else if (fs::exists(fs::path(projectRoot) / "Cargo.toml")) {
            info.language = "Rust"; info.framework = "Cargo";
            info.buildCmd = "cargo build"; info.testCmd = "cargo test";
        } else if (fs::exists(fs::path(projectRoot) / "go.mod")) {
            info.language = "Go"; info.framework = "Go modules";
            info.buildCmd = "go build ./..."; info.testCmd = "go test ./...";
        } else if (fs::exists(fs::path(projectRoot) / "requirements.txt") ||
                   fs::exists(fs::path(projectRoot) / "setup.py")) {
            info.language = "Python"; info.framework = "pip";
            info.buildCmd = ""; info.testCmd = "pytest";
        } else if (fs::exists(fs::path(projectRoot) / "pom.xml")) {
            info.language = "Java"; info.framework = "Maven";
            info.buildCmd = "mvn compile"; info.testCmd = "mvn test";
        } else if (fs::exists(fs::path(projectRoot) / "Makefile")) {
            info.language = "C/C++"; info.framework = "Make";
            info.buildCmd = "make"; info.testCmd = "make test";
        }
        return info;
    }

    static std::string getContextHint(const ProjectInfo& info) {
        if (info.language.empty()) return "";
        std::string hint = "Project: " + info.language;
        if (!info.framework.empty()) hint += " (" + info.framework + ")";
        if (!info.buildCmd.empty()) hint += " | Build: " + info.buildCmd;
        if (!info.testCmd.empty()) hint += " | Test: " + info.testCmd;
        return hint;
    }
};

} // namespace closecrab
