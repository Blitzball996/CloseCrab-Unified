#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace closecrab {

// Memory types matching JackProAi's memory system
enum class MemoryType {
    USER,       // User profile, preferences, role
    FEEDBACK,   // Corrections and confirmed approaches
    PROJECT,    // Ongoing work, goals, decisions
    REFERENCE   // Pointers to external resources
};

inline std::string memoryTypeName(MemoryType t) {
    switch (t) {
        case MemoryType::USER: return "user";
        case MemoryType::FEEDBACK: return "feedback";
        case MemoryType::PROJECT: return "project";
        case MemoryType::REFERENCE: return "reference";
    }
    return "unknown";
}

inline MemoryType parseMemoryType(const std::string& s) {
    if (s == "user") return MemoryType::USER;
    if (s == "feedback") return MemoryType::FEEDBACK;
    if (s == "project") return MemoryType::PROJECT;
    if (s == "reference") return MemoryType::REFERENCE;
    return MemoryType::PROJECT;
}

struct FileMemory {
    std::string filename;
    std::string name;
    std::string description;
    MemoryType type;
    std::string content;
};

// Manages .claude/memory/ directory with MEMORY.md index
// Each memory is a separate .md file with YAML frontmatter
class FileMemoryManager {
public:
    explicit FileMemoryManager(const std::string& projectRoot)
        : memoryDir_(std::filesystem::path(projectRoot) / ".claude" / "memory") {}

    // Save a memory to file and update MEMORY.md index
    bool saveMemory(const std::string& filename, const std::string& name,
                    const std::string& description, MemoryType type,
                    const std::string& content) {
        namespace fs = std::filesystem;
        fs::create_directories(memoryDir_);

        std::string fname = filename;
        if (fname.find(".md") == std::string::npos) fname += ".md";

        // Write memory file with frontmatter
        std::ofstream f(memoryDir_ / fname);
        if (!f.is_open()) return false;

        f << "---\n";
        f << "name: " << name << "\n";
        f << "description: " << description << "\n";
        f << "type: " << memoryTypeName(type) << "\n";
        f << "---\n\n";
        f << content << "\n";
        f.close();

        // Update MEMORY.md index
        updateIndex(fname, name, description);
        spdlog::info("Saved memory: {} ({})", name, fname);
        return true;
    }

    // Remove a memory file and update index
    bool removeMemory(const std::string& filename) {
        namespace fs = std::filesystem;
        std::string fname = filename;
        if (fname.find(".md") == std::string::npos) fname += ".md";

        fs::path filepath = memoryDir_ / fname;
        if (!fs::exists(filepath)) return false;

        fs::remove(filepath);
        rebuildIndex();
        return true;
    }

    // Load all memories from directory
    std::vector<FileMemory> loadAll() const {
        namespace fs = std::filesystem;
        std::vector<FileMemory> memories;
        if (!fs::exists(memoryDir_)) return memories;

        for (const auto& entry : fs::directory_iterator(memoryDir_)) {
            if (entry.path().extension() != ".md") continue;
            if (entry.path().filename() == "MEMORY.md") continue;

            auto mem = loadMemoryFile(entry.path());
            if (!mem.filename.empty()) {
                memories.push_back(std::move(mem));
            }
        }
        return memories;
    }

    // Load MEMORY.md index content (for system prompt injection)
    std::string loadIndex() const {
        namespace fs = std::filesystem;
        fs::path indexPath = memoryDir_ / "MEMORY.md";
        if (!fs::exists(indexPath)) return "";

        std::ifstream f(indexPath);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

    // Load memories relevant to a query (simple keyword matching)
    std::vector<FileMemory> loadRelevant(const std::string& query, int maxCount = 5) const {
        auto all = loadAll();
        std::vector<std::pair<int, FileMemory*>> scored;

        std::string queryLower = toLower(query);
        for (auto& mem : all) {
            int score = 0;
            std::string nameLower = toLower(mem.name);
            std::string descLower = toLower(mem.description);
            std::string contentLower = toLower(mem.content);

            if (nameLower.find(queryLower) != std::string::npos) score += 10;
            if (descLower.find(queryLower) != std::string::npos) score += 5;
            // Check individual words
            std::istringstream iss(queryLower);
            std::string word;
            while (iss >> word) {
                if (word.size() < 3) continue;
                if (contentLower.find(word) != std::string::npos) score += 2;
                if (nameLower.find(word) != std::string::npos) score += 3;
            }
            if (score > 0) scored.push_back({score, &mem});
        }

        std::sort(scored.begin(), scored.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        std::vector<FileMemory> result;
        int limit = maxCount < (int)scored.size() ? maxCount : (int)scored.size();
        for (int i = 0; i < limit; i++) {
            result.push_back(*scored[i].second);
        }
        return result;
    }

    bool exists() const { return std::filesystem::exists(memoryDir_); }
    std::string getMemoryDir() const { return memoryDir_.string(); }

private:
    FileMemory loadMemoryFile(const std::filesystem::path& path) const {
        FileMemory mem;
        std::ifstream f(path);
        if (!f.is_open()) return mem;

        std::string line;
        bool inFrontmatter = false;
        bool pastFrontmatter = false;
        std::string content;

        while (std::getline(f, line)) {
            if (line == "---" && !inFrontmatter && !pastFrontmatter) {
                inFrontmatter = true;
                continue;
            }
            if (line == "---" && inFrontmatter) {
                inFrontmatter = false;
                pastFrontmatter = true;
                continue;
            }
            if (inFrontmatter) {
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = trim(line.substr(0, colon));
                    std::string val = trim(line.substr(colon + 1));
                    if (key == "name") mem.name = val;
                    else if (key == "description") mem.description = val;
                    else if (key == "type") mem.type = parseMemoryType(val);
                }
            } else {
                content += line + "\n";
            }
        }

        mem.filename = path.filename().string();
        mem.content = trim(content);
        return mem;
    }

    void updateIndex(const std::string& filename, const std::string& name,
                     const std::string& description) {
        namespace fs = std::filesystem;
        fs::path indexPath = memoryDir_ / "MEMORY.md";

        // Read existing index
        std::string existing;
        if (fs::exists(indexPath)) {
            std::ifstream f(indexPath);
            existing = std::string(std::istreambuf_iterator<char>(f), {});
        }

        // Check if entry already exists, update it
        std::string entry = "- [" + name + "](" + filename + ") — " + description;
        if (existing.find(filename) != std::string::npos) {
            // Replace existing line
            std::istringstream iss(existing);
            std::ostringstream oss;
            std::string line;
            while (std::getline(iss, line)) {
                if (line.find(filename) != std::string::npos) {
                    oss << entry << "\n";
                } else {
                    oss << line << "\n";
                }
            }
            existing = oss.str();
        } else {
            existing += entry + "\n";
        }

        std::ofstream f(indexPath);
        f << existing;
    }

    void rebuildIndex() {
        auto memories = loadAll();
        std::ofstream f(memoryDir_ / "MEMORY.md");
        for (const auto& mem : memories) {
            f << "- [" << mem.name << "](" << mem.filename << ") — " << mem.description << "\n";
        }
    }

    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\n\r");
        return s.substr(start, end - start + 1);
    }

    static std::string toLower(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    std::filesystem::path memoryDir_;
};

} // namespace closecrab
