#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {
namespace fs = std::filesystem;

class MagicDocsService {
public:
    static MagicDocsService& getInstance() {
        static MagicDocsService instance;
        return instance;
    }

    template <typename ApiClient>
    std::string generate(const std::string& filePath, ApiClient* apiClient) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string content = readFile(filePath);
        if (content.empty()) {
            spdlog::warn("MagicDocs: file is empty or unreadable: {}", filePath);
            return {};
        }

        if (apiClient) {
            return generateWithLLM(filePath, content, apiClient);
        }
        return generateLocally(filePath, content);
    }

    template <typename ApiClient>
    std::string generateForProject(const std::string& projectRoot, ApiClient* apiClient) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Collect source files
        std::vector<std::string> sourceFiles;
        for (const auto& ext : {".h", ".hpp", ".cpp", ".c", ".cc", ".cxx"}) {
            for (const auto& entry : fs::recursive_directory_iterator(
                     projectRoot, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file() && entry.path().extension() == ext) {
                    sourceFiles.push_back(entry.path().string());
                }
            }
        }

        if (sourceFiles.empty()) {
            spdlog::warn("MagicDocs: no source files found in {}", projectRoot);
            return {};
        }

        if (apiClient) {
            // Build a summary of the project structure
            std::string structure;
            for (const auto& f : sourceFiles) {
                auto rel = fs::relative(fs::path(f), fs::path(projectRoot));
                structure += "- " + rel.string() + "\n";
            }

            std::string prompt =
                "Generate a README-style project overview for a C++ project with these files:\n\n"
                + structure +
                "\nProvide: project description, directory structure explanation, "
                "build instructions, and key components.";

            try {
                return apiClient->complete(prompt);
            } catch (const std::exception& e) {
                spdlog::warn("MagicDocs: LLM project overview failed: {}", e.what());
            }
        }

        // Local fallback: generate basic structure overview
        std::ostringstream oss;
        oss << "# Project Overview\n\n";
        oss << "Source files: " << sourceFiles.size() << "\n\n";
        oss << "## Structure\n\n";

        std::map<std::string, int> dirCounts;
        for (const auto& f : sourceFiles) {
            auto rel = fs::relative(fs::path(f), fs::path(projectRoot));
            auto dir = rel.parent_path().string();
            if (dir.empty()) dir = ".";
            dirCounts[dir]++;
        }

        for (const auto& [dir, count] : dirCounts) {
            oss << "- `" << dir << "/` (" << count << " files)\n";
        }

        return oss.str();
    }

private:
    MagicDocsService() = default;
    mutable std::mutex mutex_;

    static std::string readFile(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return {};
        return std::string((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    }

    template <typename ApiClient>
    std::string generateWithLLM(const std::string& filePath,
                                const std::string& content,
                                ApiClient* apiClient) {
        std::string prompt =
            "Generate concise documentation for this code.\n\n"
            "File: " + filePath + "\n\n```\n" + content + "\n```";

        try {
            return apiClient->complete(prompt);
        } catch (const std::exception& e) {
            spdlog::warn("MagicDocs: LLM generation failed: {}", e.what());
            return generateLocally(filePath, content);
        }
    }

    std::string generateLocally(const std::string& filePath, const std::string& content) {
        std::ostringstream doc;
        std::string ext = fs::path(filePath).extension().string();
        std::string filename = fs::path(filePath).filename().string();

        doc << "# " << filename << "\n\n";

        // Parse function/class signatures
        std::istringstream stream(content);
        std::string line;
        std::vector<std::string> signatures;

        // Simple regex patterns for C/C++ signatures
        std::regex funcRegex(R"(^\s*(?:(?:static|virtual|inline|const|extern)\s+)*[\w:*&<>]+\s+(\w+)\s*\()");
        std::regex classRegex(R"(^\s*(?:class|struct)\s+(\w+))");

        while (std::getline(stream, line)) {
            std::smatch match;
            if (std::regex_search(line, match, classRegex)) {
                signatures.push_back("class/struct " + match[1].str());
            } else if (std::regex_search(line, match, funcRegex)) {
                std::string name = match[1].str();
                // Skip common non-function matches
                if (name != "if" && name != "for" && name != "while" &&
                    name != "switch" && name != "return") {
                    signatures.push_back("function " + name + "()");
                }
            }
        }

        if (!signatures.empty()) {
            doc << "## Declarations\n\n";
            for (const auto& sig : signatures) {
                doc << "- `" << sig << "`\n";
            }
        } else {
            doc << "No function or class declarations detected.\n";
        }

        return doc.str();
    }
};

} // namespace closecrab
