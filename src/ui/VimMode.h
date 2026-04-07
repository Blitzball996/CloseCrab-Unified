#pragma once

#include <string>
#include <functional>
#include <iostream>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#endif

namespace closecrab {

enum class VimModeType {
    NORMAL,
    INSERT,
    COMMAND
};

inline std::string vimModeName(VimModeType m) {
    switch (m) {
        case VimModeType::NORMAL: return "NORMAL";
        case VimModeType::INSERT: return "INSERT";
        case VimModeType::COMMAND: return "COMMAND";
    }
    return "?";
}

// Lightweight Vim-style input handler for the REPL
// Supports: i/a (insert), Esc (normal), dd (clear line), :q, :w
class VimInput {
public:
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; mode_ = VimModeType::INSERT; }
    VimModeType getMode() const { return mode_; }

    // Process a full line of input with vim keybindings
    // Returns the final input string, or empty if command handled internally
    struct Result {
        std::string text;
        bool shouldQuit = false;
        bool handled = false;  // true if vim consumed the input
    };

    Result processLine(const std::string& rawInput) {
        if (!enabled_) return {rawInput, false, false};

        // In INSERT mode, most input passes through
        if (mode_ == VimModeType::INSERT) {
            // Check for Esc at end (user typed text then Esc)
            // In line-based input, we just pass through
            return {rawInput, false, false};
        }

        // NORMAL mode commands
        if (mode_ == VimModeType::NORMAL) {
            return processNormalMode(rawInput);
        }

        // COMMAND mode (:commands)
        if (mode_ == VimModeType::COMMAND) {
            return processCommandMode(rawInput);
        }

        return {rawInput, false, false};
    }

    // Get the mode indicator string for the prompt
    std::string getModeIndicator() const {
        if (!enabled_) return "";
        switch (mode_) {
            case VimModeType::NORMAL:  return "\033[1;33m[N]\033[0m ";
            case VimModeType::INSERT:  return "\033[1;32m[I]\033[0m ";
            case VimModeType::COMMAND: return "\033[1;36m[:]\033[0m ";
        }
        return "";
    }

    // Handle single-character input for mode switching
    // Returns true if the character was consumed by vim
    bool handleChar(char c) {
        if (!enabled_) return false;

        if (mode_ == VimModeType::NORMAL) {
            switch (c) {
                case 'i': mode_ = VimModeType::INSERT; return true;
                case 'a': mode_ = VimModeType::INSERT; return true;
                case 'A': mode_ = VimModeType::INSERT; return true;
                case ':': mode_ = VimModeType::COMMAND; return true;
                case 27:  return true; // Esc in normal mode = noop
                default: return false;
            }
        }

        if (mode_ == VimModeType::INSERT) {
            if (c == 27) { // Esc
                mode_ = VimModeType::NORMAL;
                return true;
            }
            return false; // Pass through to input
        }

        return false;
    }

private:
    Result processNormalMode(const std::string& input) {
        if (input.empty()) return {"", false, true};

        // Single char commands
        if (input == "i" || input == "a" || input == "A") {
            mode_ = VimModeType::INSERT;
            return {"", false, true};
        }
        if (input == "dd") {
            // Clear current line (in REPL context: clear pending input)
            return {"", false, true};
        }
        if (input.size() >= 1 && input[0] == ':') {
            return processCommandMode(input.substr(1));
        }

        // If it looks like regular text, switch to insert and pass through
        if (input.size() > 1) {
            mode_ = VimModeType::INSERT;
            return {input, false, false};
        }

        return {"", false, true};
    }

    Result processCommandMode(const std::string& cmd) {
        mode_ = VimModeType::NORMAL;

        if (cmd == "q" || cmd == "q!" || cmd == "quit") {
            return {"", true, true};
        }
        if (cmd == "w") {
            // In REPL context, :w doesn't make sense — ignore
            return {"", false, true};
        }
        if (cmd == "wq") {
            return {"", true, true};
        }
        if (cmd == "set novim" || cmd == "set novm") {
            enabled_ = false;
            return {"", false, true};
        }

        // Unknown command — treat as regular input
        return {":" + cmd, false, false};
    }

    bool enabled_ = false;
    VimModeType mode_ = VimModeType::INSERT;
};

} // namespace closecrab
