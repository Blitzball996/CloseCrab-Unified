#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace closecrab {

class MagicDocs {
public:
    struct ProjectInfo {
        std::string name;
        std::string language;
        std::vector<std::string> sourceFiles;
        std::vector<std::string> configFiles;
        int totalLines = 0;
        std::string buildSystem;
    };

    // Scan a project directory and generate documentation skeleton
    static std::string generate(const std::string& projectPath) {
        namespace fs = std::filesystem;
        if (!fs::exists(projectPath)) return "Error: path not found";

        ProjectInfo info = scanProject(projectPath);
        return buildDocumentation(info, projectPath);
    }

private:
    static ProjectInfo scanProject(const std::string& path) {
        namespace fs = std::filesystem;
        ProjectInfo info;
        info.name = fs::path(path).filename().string();

        // Detect build system
        if (fs::exists(path + "/CMakeLists.txt")) info.buildSystem = "CMake";
        else if (fs::exists(path + "/package.json")) info.buildSystem = "npm";
        else if (fs::exists(path + "/Cargo.toml")) info.buildSystem = "Cargo";
        else if (fs::exists(path + "/Makefile")) info.buildSystem = "Make";
        else if (fs::exists(path + "/go.mod")) info.buildSystem = "Go modules";
        else if (fs::exists(path + "/pom.xml")) info.buildSystem = "Maven";

        // Detect language and count files
        std::map<std::string, int> extCount;
        for (auto& entry : fs::recursive_directory_iterator(path,
                fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (ext.empty()) continue;
            extCount[ext]++;

            // Count lines for source files
            if (ext == ".cpp" || ext == ".h" || ext == ".py" || ext == ".js" ||
                ext == ".ts" || ext == ".rs" || ext == ".go" || ext == ".java") {
                info.sourceFiles.push_back(fs::relative(entry.path(), path).string());
                std::ifstream f(entry.path());
                info.totalLines += (int)std::count(std::istreambuf_iterator<char>(f),
                                                    std::istreambuf_iterator<char>(), '\n');
            }
            if (ext == ".json" || ext == ".yaml" || ext == ".yml" || ext == ".toml") {
                info.configFiles.push_back(fs::relative(entry.path(), path).string());
            }
        }

        // Determine primary language
        int maxCount = 0;
        for (const auto& [ext, count] : extCount) {
            if (count > maxCount) {
                maxCount = count;
                if (ext == ".cpp" || ext == ".h") info.language = "C++";
                else if (ext == ".py") info.language = "Python";
                else if (ext == ".js") info.language = "JavaScript";
                else if (ext == ".ts") info.language = "TypeScript";
                else if (ext == ".rs") info.language = "Rust";
                else if (ext == ".go") info.language = "Go";
                else if (ext == ".java") info.language = "Java";
            }
        }

        return info;
    }

    static std::string buildDocumentation(const ProjectInfo& info, const std::string& path) {
        std::ostringstream doc;
        doc << "# " << info.name << "\n\n";
        doc << "## Overview\n\n";
        doc << "- **Language**: " << info.language << "\n";
        doc << "- **Build System**: " << info.buildSystem << "\n";
        doc << "- **Source Files**: " << info.sourceFiles.size() << "\n";
        doc << "- **Total Lines**: " << info.totalLines << "\n\n";

        doc << "## Project Structure\n\n```\n";
        // Show top-level directories
        namespace fs = std::filesystem;
        for (auto& entry : fs::directory_iterator(path)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                if (name[0] == '.' || name == "node_modules" || name == "build" ||
                    name == "target" || name == "__pycache__") continue;
                doc << name << "/\n";
            }
        }
        doc << "```\n\n";

        doc << "## Getting Started\n\n";
        if (info.buildSystem == "CMake") {
            doc << "```bash\nmkdir build && cd build\ncmake ..\ncmake --build .\n```\n";
        } else if (info.buildSystem == "npm") {
            doc << "```bash\nnpm install\nnpm start\n```\n";
        } else if (info.buildSystem == "Cargo") {
            doc << "```bash\ncargo build --release\ncargo run\n```\n";
        }

        doc << "\n## Configuration\n\n";
        for (const auto& cfg : info.configFiles) {
            if (info.configFiles.size() > 10) break;
            doc << "- `" << cfg << "`\n";
        }

        return doc.str();
    }
};

} // namespace closecrab
