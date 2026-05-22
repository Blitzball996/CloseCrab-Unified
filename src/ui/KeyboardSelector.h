#pragma once
#include <string>
#include <vector>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif

namespace closecrab {

struct SelectorResult {
    int index = 0;          // Selected option index (-1 for custom)
    std::string customText; // User-typed text (if "Type response" selected)
};

class KeyboardSelector {
public:
    // Select from vertical list with optional custom text input
    static SelectorResult select(const std::vector<std::string>& options,
                                  int defaultIdx = 0,
                                  bool allowCustom = true) {
        SelectorResult result;
#ifdef _WIN32
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
                    // Read line from console
                    char buf[256] = {};
                    DWORD read;
                    ReadConsoleA(hStdin, buf, 255, &read, nullptr);
                    result.customText = std::string(buf, read);
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
            } else if (ch == 'y' || ch == 'Y') {
                clearLines(totalOptions);
                result.index = 0;
                return result;
            } else if (ch == 'n' || ch == 'N') {
                clearLines(totalOptions);
                result.index = 1;
                return result;
            } else if (ch == 'a' || ch == 'A') {
                clearLines(totalOptions);
                result.index = (int)options.size() > 2 ? 2 : 0;
                return result;
            }
        }
#else
        result.index = defaultIdx;
        return result;
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
