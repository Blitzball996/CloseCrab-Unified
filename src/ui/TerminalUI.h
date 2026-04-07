#pragma once

#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace closecrab {

// ANSI color helpers
namespace ansi {
    inline std::string reset()   { return "\033[0m"; }
    inline std::string bold()    { return "\033[1m"; }
    inline std::string dim()     { return "\033[2m"; }
    inline std::string italic()  { return "\033[3m"; }
    inline std::string red()     { return "\033[31m"; }
    inline std::string green()   { return "\033[32m"; }
    inline std::string yellow()  { return "\033[33m"; }
    inline std::string blue()    { return "\033[34m"; }
    inline std::string magenta() { return "\033[35m"; }
    inline std::string cyan()    { return "\033[36m"; }
    inline std::string gray()    { return "\033[90m"; }
    inline std::string bgGray()  { return "\033[100m"; }
}

// Spinner for long-running operations
class Spinner {
public:
    void start(const std::string& message = "") {
        if (running_.load()) return;
        running_ = true;
        message_ = message;
        thread_ = std::thread([this]() {
            const char* frames[] = {"|","/","-","\\","|","/","-","\\"};
            int i = 0;
            while (running_.load()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    std::cout << "\r" << ansi::cyan() << frames[i % 8]
                              << ansi::reset() << " " << message_ << "   " << std::flush;
                }
                i++;
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
            std::cout << "\r" << std::string(message_.size() + 10, ' ') << "\r" << std::flush;
        });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    void setMessage(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        message_ = msg;
    }

    ~Spinner() { stop(); }

private:
    std::atomic<bool> running_{false};
    std::string message_;
    std::thread thread_;
    std::mutex mutex_;
};

// Simple markdown-to-ANSI renderer for terminal output
class MarkdownRenderer {
public:
    // Render markdown text with ANSI colors
    static std::string render(const std::string& text) {
        std::istringstream iss(text);
        std::string line;
        std::ostringstream out;
        bool inCodeBlock = false;
        std::string codeLang;

        while (std::getline(iss, line)) {
            // Code block toggle
            if (line.size() >= 3 && line.substr(0, 3) == "```") {
                if (!inCodeBlock) {
                    codeLang = line.size() > 3 ? line.substr(3) : "";
                    out << ansi::dim() << "  " << codeLang << ansi::reset() << "\n";
                    inCodeBlock = true;
                } else {
                    inCodeBlock = false;
                    out << ansi::reset();
                }
                continue;
            }

            if (inCodeBlock) {
                out << ansi::gray() << "  " << line << ansi::reset() << "\n";
                continue;
            }

            // Headers
            if (line.size() > 2 && line[0] == '#') {
                int level = 0;
                while (level < (int)line.size() && line[level] == '#') level++;
                std::string content = line.substr(level);
                while (!content.empty() && content[0] == ' ') content.erase(0, 1);
                if (level == 1) out << ansi::bold() << ansi::cyan() << content << ansi::reset() << "\n";
                else if (level == 2) out << ansi::bold() << content << ansi::reset() << "\n";
                else out << ansi::bold() << content << ansi::reset() << "\n";
                continue;
            }

            // Bullet points
            if (line.size() > 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ') {
                out << ansi::cyan() << "  " << line[0] << ansi::reset() << line.substr(1) << "\n";
                continue;
            }

            // Inline code: `code`
            out << renderInline(line) << "\n";
        }

        if (inCodeBlock) out << ansi::reset();
        return out.str();
    }

private:
    static std::string renderInline(const std::string& line) {
        std::string result;
        bool inCode = false;
        bool inBold = false;

        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == '`' && !inBold) {
                inCode = !inCode;
                result += inCode ? ansi::bgGray() : ansi::reset();
                continue;
            }
            if (i + 1 < line.size() && line[i] == '*' && line[i+1] == '*' && !inCode) {
                inBold = !inBold;
                result += inBold ? ansi::bold() : ansi::reset();
                i++; // skip second *
                continue;
            }
            result += line[i];
        }
        if (inCode || inBold) result += ansi::reset();
        return result;
    }
};

// Input history for up/down arrow navigation
class InputHistory {
public:
    void add(const std::string& line) {
        if (line.empty()) return;
        if (!history_.empty() && history_.back() == line) return; // dedup
        history_.push_back(line);
        if (history_.size() > maxSize_) history_.erase(history_.begin());
        pos_ = (int)history_.size(); // reset to end
    }

    std::string prev() {
        if (history_.empty()) return "";
        if (pos_ > 0) pos_--;
        return history_[pos_];
    }

    std::string next() {
        if (pos_ < (int)history_.size() - 1) {
            pos_++;
            return history_[pos_];
        }
        pos_ = (int)history_.size();
        return "";
    }

    void resetPos() { pos_ = (int)history_.size(); }

private:
    std::vector<std::string> history_;
    int pos_ = 0;
    size_t maxSize_ = 500;
};

// Table formatter for aligned output
class TableFormatter {
public:
    void addRow(const std::vector<std::string>& row) { rows_.push_back(row); }

    std::string render() const {
        if (rows_.empty()) return "";

        // Calculate column widths
        std::vector<size_t> widths;
        for (const auto& row : rows_) {
            if (widths.size() < row.size()) widths.resize(row.size(), 0);
            for (size_t i = 0; i < row.size(); i++) {
                widths[i] = std::max(widths[i], row[i].size());
            }
        }

        std::ostringstream out;
        bool isHeader = true;
        for (const auto& row : rows_) {
            for (size_t i = 0; i < row.size(); i++) {
                if (i > 0) out << "  ";
                out << row[i];
                if (i < row.size() - 1) {
                    for (size_t j = row[i].size(); j < widths[i]; j++) out << ' ';
                }
            }
            out << "\n";
            if (isHeader) {
                for (size_t i = 0; i < widths.size(); i++) {
                    if (i > 0) out << "  ";
                    out << std::string(widths[i], '-');
                }
                out << "\n";
                isHeader = false;
            }
        }
        return out.str();
    }

private:
    std::vector<std::vector<std::string>> rows_;
};

// Get terminal width
inline int getTerminalWidth() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
    return 80;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) return w.ws_col;
    return 80;
#endif
}

} // namespace closecrab
