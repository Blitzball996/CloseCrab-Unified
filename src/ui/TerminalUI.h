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

// Global stdout mutex: ALL terminal output must go through this.
// Multiple threads write to stdout (spinner, sub-agents, main thread).
// Without a single mutex, concurrent writes corrupt the Windows console
// buffer → ACCESS_VIOLATION (0xC0000005) on the next write.
// claude-code doesn't need this because Node.js is single-threaded.
inline std::mutex& getStdoutMutex() {
    static std::mutex m;
    return m;
}

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
    Spinner() {
        // Single persistent thread — never destroyed until program exit.
        // Repeated thread create/join was causing heap corruption on Windows.
        worker_ = std::thread([this]() {
            while (!shutdown_.load()) {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return active_.load() || shutdown_.load(); });
                if (shutdown_.load()) break;
                lock.unlock();
                const char* frames[] = {"|","/","-","\\","|","/","-","\\"};
                int i = 0;
                while (active_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                    if (!active_.load()) break;
                    {
                        std::lock_guard<std::mutex> lk(getStdoutMutex());
                        // Re-check under the lock that stop() also takes: this
                        // guarantees that once stop() has returned, the worker can
                        // NEVER write another frame (so a callback that called
                        // stop() owns the console alone). Without this re-check the
                        // worker could emit one stale frame after stop() and race
                        // the callback's write.
                        if (!active_.load()) break;
                        std::cout << "\r" << ansi::cyan() << frames[i % 8] << ansi::reset() << std::flush;
                    }
                    i++;
                }
            }
        });
    }

    void start(const char* message = nullptr) {
        stop();
        if (!message || !message[0]) {
            message = getRandomVerb();
        }
        size_t len = strlen(message);
        if (len >= sizeof(message_)) len = sizeof(message_) - 1;
        memcpy(message_, message, len);
        message_[len] = '\0';

        if (std::getenv("CLOSECRAB_WEB")) {
            std::cout << "<<<CCSPIN:START:" << message_ << ">>>\n" << std::flush;
            return;
        }
        {
            // MUST hold the stdout mutex: the worker thread, the streaming
            // callbacks, and start/stop all write the console. Without this the
            // worker (which DOES lock) races the main thread (which didn't) on
            // std::cout → streambuf/heap corruption → ACCESS_VIOLATION (the crash
            // that shows up when typing during a turn). See getStdoutMutex() note.
            std::lock_guard<std::mutex> lk(getStdoutMutex());
            std::cout << "\r" << ansi::cyan() << "|" << ansi::reset()
                      << " " << message_ << "   " << std::flush;
        }
        active_ = true;
        cv_.notify_one();
    }

    void start(const std::string& message) {
        start(message.c_str());
    }

private:
    static const char* getRandomVerb() {
        static const char* VERBS[] = {
            "Accomplishing", "Actioning", "Actualizing", "Architecting",
            "Baking", "Beaming", "Beboppin'", "Befuddling",
            "Billowing", "Blanching", "Bloviating", "Boogieing",
            "Boondoggling", "Booping", "Bootstrapping", "Brewing",
            "Bunning", "Burrowing", "Calculating", "Canoodling",
            "Caramelizing", "Cascading", "Catapulting", "Cerebrating",
            "Channeling", "Channelling", "Choreographing", "Churning",
            "Clauding", "Coalescing", "Cogitating", "Combobulating",
            "Composing", "Computing", "Concocting", "Considering",
            "Contemplating", "Cooking", "Crafting", "Creating",
            "Crunching", "Crystallizing", "Cultivating", "Deciphering",
            "Deliberating", "Determining", "Dilly-dallying", "Discombobulating",
            "Doing", "Doodling", "Drizzling", "Ebbing",
            "Effecting", "Elucidating", "Embellishing", "Enchanting",
            "Envisioning", "Evaporating", "Fermenting", "Fiddle-faddling",
            "Finagling", "Flibbertigibbeting", "Flowing", "Flummoxing",
            "Fluttering", "Forging", "Forming", "Frolicking",
            "Frosting", "Gallivanting", "Galloping", "Garnishing",
            "Generating", "Gesticulating", "Germinating", "Gitifying",
            "Grooving", "Gusting", "Harmonizing", "Hashing",
            "Hatching", "Herding", "Honking", "Hullaballooing",
            "Hyperspacing", "Ideating", "Imagining", "Improvising",
            "Incubating", "Inferring", "Infusing", "Ionizing",
            "Jitterbugging", "Julienning", "Kneading", "Leavening",
            "Levitating", "Lollygagging", "Manifesting", "Marinating",
            "Meandering", "Metamorphosing", "Misting", "Moonwalking",
            "Moseying", "Mulling", "Mustering", "Musing",
            "Nebulizing", "Nesting", "Newspapering", "Noodling",
            "Nucleating", "Orbiting", "Orchestrating", "Osmosing",
            "Perambulating", "Percolating", "Perusing", "Philosophising",
            "Photosynthesizing", "Pollinating", "Pondering", "Pontificating",
            "Pouncing", "Precipitating", "Prestidigitating", "Processing",
            "Proofing", "Propagating", "Puttering", "Puzzling",
            "Quantumizing", "Razzle-dazzling", "Razzmatazzing", "Recombobulating",
            "Reticulating", "Roosting", "Ruminating", "Scampering",
            "Schlepping", "Scurrying", "Seasoning", "Shenaniganing",
            "Shimmying", "Simmering", "Skedaddling", "Sketching",
            "Slithering", "Smooshing", "Sock-hopping", "Spelunking",
            "Spinning", "Sprouting", "Stewing", "Sublimating",
            "Swirling", "Swooping", "Symbioting", "Synthesizing",
            "Tempering", "Thinking", "Thundering", "Tinkering",
            "Tomfoolering", "Topsy-turvying", "Transfiguring", "Transmuting",
            "Twisting", "Undulating", "Unfurling", "Unravelling",
            "Vibing", "Waddling", "Wandering", "Warping",
            "Whatchamacalliting", "Whirlpooling", "Whirring", "Whisking",
            "Wibbling", "Working", "Wrangling", "Zesting",
            "Zigzagging"
        };
        static constexpr int VERB_COUNT = 188;
        return VERBS[rand() % VERB_COUNT];
    }

public:

    void stop() {
        if (!active_.exchange(false)) return;
        // Acquire the stdout mutex AFTER clearing active_: this both serializes
        // our own write AND guarantees the worker thread is quiescent on return
        // (if it's mid-frame it holds the mutex, so we block until it releases,
        // then it sees active_==false and stops). So any callback that calls
        // stop() can safely write std::cout afterwards with no other writer.
        std::lock_guard<std::mutex> lk(getStdoutMutex());
        if (!std::getenv("CLOSECRAB_WEB")) {
            std::cout << "\r                                                            \r" << std::flush;
        } else {
            std::cout << "<<<CCSPIN:STOP>>>\n" << std::flush;
        }
    }

    void setMessage(const std::string& msg) {
        size_t len = msg.size();
        if (len >= sizeof(message_)) len = sizeof(message_) - 1;
        memcpy(message_, msg.c_str(), len);
        message_[len] = '\0';
    }

    ~Spinner() {
        active_ = false;
        shutdown_ = true;
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

private:
    std::atomic<bool> active_{false};
    std::atomic<bool> shutdown_{false};
    char message_[64] = {};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
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
