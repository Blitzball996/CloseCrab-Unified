#pragma once
#include <string>
#include <vector>
#include <set>
#include <mutex>

namespace closecrab {

class FileChangeTracker {
public:
    static FileChangeTracker& getInstance() {
        static FileChangeTracker instance;
        return instance;
    }

    void trackWrite(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        writtenFiles_.insert(path);
    }
    void trackEdit(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        editedFiles_.insert(path);
    }
    void trackRead(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        readFiles_.insert(path);
    }

    std::string getSummary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string s = "Session file activity:\n";
        if (!writtenFiles_.empty()) {
            s += "  Created (" + std::to_string(writtenFiles_.size()) + "): ";
            int count = 0;
            for (const auto& f : writtenFiles_) {
                if (count++ > 5) { s += "..."; break; }
                size_t slash = f.find_last_of("/\\");
                s += (slash != std::string::npos ? f.substr(slash+1) : f) + " ";
            }
            s += "\n";
        }
        if (!editedFiles_.empty()) {
            s += "  Modified (" + std::to_string(editedFiles_.size()) + "): ";
            int count = 0;
            for (const auto& f : editedFiles_) {
                if (count++ > 5) { s += "..."; break; }
                size_t slash = f.find_last_of("/\\");
                s += (slash != std::string::npos ? f.substr(slash+1) : f) + " ";
            }
            s += "\n";
        }
        s += "  Read: " + std::to_string(readFiles_.size()) + " files\n";
        return s;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        writtenFiles_.clear();
        editedFiles_.clear();
        readFiles_.clear();
    }

private:
    FileChangeTracker() = default;
    mutable std::mutex mutex_;
    std::set<std::string> writtenFiles_;
    std::set<std::string> editedFiles_;
    std::set<std::string> readFiles_;
};

} // namespace closecrab
