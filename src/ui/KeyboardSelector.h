#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <thread>
#include <chrono>
#include "ConsoleInputGuard.h"
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include "PosixTty.h"
#include <termios.h>
#include <unistd.h>
#include <iostream>
#endif

namespace closecrab {

struct SelectorResult {
    int index = 0;          // Selected option index (-1 for custom)
    std::string customText; // User-typed text (if "Type response" selected)
};

class KeyboardSelector {
public:
    // Select from vertical list with optional custom text input.
    // enableShortcuts: y/n/a quick keys (for the Allow/Deny/AllowAll permission
    // prompt). Disable for general questions where the user may type a free-text
    // answer that legitimately starts with y/n/a.
    static SelectorResult select(const std::vector<std::string>& options,
                                  int defaultIdx = 0,
                                  bool allowCustom = true,
                                  bool enableShortcuts = true) {
        SelectorResult result;
#ifdef _WIN32
        // CRITICAL: claim console-input ownership for the whole prompt. The
        // per-turn key-watcher thread (main.cpp) polls consoleInputBusy() and
        // stops calling _getch() while this guard is held — otherwise BOTH
        // threads read the same console and the process hard-crashes (闪退).
        // This covers every selector caller (permission prompt, AskUserQuestion)
        // automatically. Give the watcher one poll-interval (>20ms) to observe
        // the flag and release the console before we read.
        ConsoleInputGuard _consoleGuard;
        std::this_thread::sleep_for(std::chrono::milliseconds(35));

        // Check if stdin is a real console
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        if (!GetConsoleMode(hStdin, &mode)) {
            result.index = defaultIdx;
            return result;
        }

        int totalOptions = (int)options.size() + (allowCustom ? 1 : 0);
        int current = defaultIdx;
        if (current < 0 || current >= totalOptions) current = 0;

        // Print initial blank lines so renderVertical can move up
        for (int i = 0; i < totalOptions; i++) printf("\n");

        while (true) {
            renderVertical(options, current, allowCustom);

            int ch = _getch();
            if (ch == 13) { // Enter
                clearLines(totalOptions);
                if (allowCustom && current == (int)options.size()) {
                    // Custom text mode
                    printf("  > ");
                    fflush(stdout);
                    result.index = -1;
                    // The surrounding turn put the console in raw mode (no echo /
                    // no line editing). Restore cooked mode just for this line so
                    // the user can see and edit what they type, then set it back.
                    DWORD prevMode = 0;
                    bool haveMode = GetConsoleMode(hStdin, &prevMode) != 0;
                    if (haveMode)
                        SetConsoleMode(hStdin, prevMode | ENABLE_ECHO_INPUT |
                                                   ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
                    // Read line via ReadConsoleW (wide chars) then convert to UTF-8.
                    // ReadConsoleA uses the ANSI code page and silently drops/mangles
                    // CJK characters — use the same ReadConsoleW path as the main
                    // input loop so Chinese/Japanese/Korean free-text answers work.
                    wchar_t wbuf[1024] = {};
                    DWORD wread = 0;
                    ReadConsoleW(hStdin, wbuf, 1023, &wread, nullptr);
                    if (haveMode) SetConsoleMode(hStdin, prevMode);
                    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)wread, nullptr, 0, nullptr, nullptr);
                    if (ulen > 0) {
                        std::string utf8(ulen, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)wread, &utf8[0], ulen, nullptr, nullptr);
                        result.customText = utf8;
                    }
                    // Trim trailing \r\n
                    while (!result.customText.empty() &&
                           (result.customText.back() == '\n' || result.customText.back() == '\r'))
                        result.customText.pop_back();
                } else {
                    result.index = current;
                }
                return result;
            } else if (ch == 27) { // Escape = Deny
                clearLines(totalOptions);
                result.index = 1; // Deny
                return result;
            } else if (ch == 0 || ch == 224) {
                int arrow = _getch();
                if (arrow == 72) current = (current - 1 + totalOptions) % totalOptions; // Up
                else if (arrow == 80) current = (current + 1) % totalOptions; // Down
            } else if (enableShortcuts && (ch == 'y' || ch == 'Y')) {
                clearLines(totalOptions);
                result.index = 0;
                return result;
            } else if (enableShortcuts && (ch == 'n' || ch == 'N')) {
                clearLines(totalOptions);
                result.index = 1;
                return result;
            } else if (enableShortcuts && (ch == 'a' || ch == 'A')) {
                clearLines(totalOptions);
                result.index = (int)options.size() > 2 ? 2 : 0;
                return result;
            }
        }
#else
        // ---- POSIX (macOS/Linux) interactive selector ----------------------
        // Previously this just returned `defaultIdx` WITHOUT reading the keyboard,
        // so the permission prompt always auto-picked option 0 (Allow) and the
        // user could never choose Deny. Now we drive a real arrow-key/shortcut
        // selector via termios raw mode.
        //
        // Claim console-input ownership so the per-turn Esc-watcher yields stdin
        // (otherwise both threads read the same fd → race/crash). Give it one
        // poll interval to observe the flag before we start reading.
        ConsoleInputGuard _consoleGuard;
        std::this_thread::sleep_for(std::chrono::milliseconds(35));

        // Not a TTY (piped stdin): fall back to the default, can't prompt.
        if (!isatty(STDIN_FILENO)) {
            result.index = defaultIdx;
            return result;
        }

        // Save current termios and set raw mode for the selector. Symmetric
        // save/restore makes this safe whether or not the outer turn already put
        // the terminal in raw mode (PosixTty): we restore exactly what we saved.
        struct termios savedTio{};
        bool haveTio = (tcgetattr(STDIN_FILENO, &savedTio) == 0);
        if (haveTio) {
            struct termios raw = savedTio;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        }

        int totalOptions = (int)options.size() + (allowCustom ? 1 : 0);
        int current = defaultIdx;
        if (current < 0 || current >= totalOptions) current = 0;

        // Print initial blank lines so renderVertical can move up.
        for (int i = 0; i < totalOptions; i++) printf("\n");

        auto restoreTio = [&]() { if (haveTio) tcsetattr(STDIN_FILENO, TCSANOW, &savedTio); };

        while (true) {
            renderVertical(options, current, allowCustom);

            int lastChar = 0;
            int key = posixReadKey(200, lastChar);   // block up to 200ms, then redraw
            if (key == PK_NONE) continue;

            if (key == PK_ENTER) {
                clearLines(totalOptions);
                if (allowCustom && current == (int)options.size()) {
                    // Custom free-text: restore cooked mode so the user sees and can
                    // edit what they type, read a line, then go back to raw.
                    printf("  > "); fflush(stdout);
                    result.index = -1;
                    restoreTio();
                    std::string line;
                    std::getline(std::cin, line);
                    if (haveTio) {
                        struct termios raw = savedTio;
                        raw.c_lflag &= ~(ICANON | ECHO);
                        raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
                        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
                    }
                    result.customText = line;
                } else {
                    result.index = current;
                }
                restoreTio();
                return result;
            } else if (key == PK_ESC) {          // Esc = Deny (matches Windows)
                clearLines(totalOptions);
                result.index = 1;
                restoreTio();
                return result;
            } else if (key == PK_UP) {
                current = (current - 1 + totalOptions) % totalOptions;
            } else if (key == PK_DOWN) {
                current = (current + 1) % totalOptions;
            } else if (key == PK_OTHER && enableShortcuts) {
                if (lastChar == 'y' || lastChar == 'Y') {
                    clearLines(totalOptions); result.index = 0; restoreTio(); return result;
                } else if (lastChar == 'n' || lastChar == 'N') {
                    clearLines(totalOptions); result.index = 1; restoreTio(); return result;
                } else if (lastChar == 'a' || lastChar == 'A') {
                    clearLines(totalOptions);
                    result.index = (int)options.size() > 2 ? 2 : 0;
                    restoreTio(); return result;
                }
            }
        }
#endif
    }

private:
    static void renderVertical(const std::vector<std::string>& options,
                                int selected, bool allowCustom) {
        // Move cursor up to redraw
        int total = (int)options.size() + (allowCustom ? 1 : 0);
        for (int i = 0; i < total; i++) printf("\033[A"); // Move up
        printf("\r");

        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) {
                printf("  \033[33m> %s\033[0m\n", options[i].c_str());
            } else {
                printf("    %s\n", options[i].c_str());
            }
        }
        if (allowCustom) {
            if (selected == (int)options.size()) {
                printf("  \033[36m> [Type response...]\033[0m\n");
            } else {
                printf("    \033[90m[Type response...]\033[0m\n");
            }
        }
        fflush(stdout);
    }

    static void clearLines(int count) {
        for (int i = 0; i < count; i++) {
            printf("\033[A\033[2K"); // Move up + clear line
        }
        printf("\r");
        fflush(stdout);
    }
};

} // namespace closecrab
