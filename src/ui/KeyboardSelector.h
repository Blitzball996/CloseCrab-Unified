#pragma once
#include <string>
#include <vector>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif

namespace closecrab {

class KeyboardSelector {
public:
    // Returns index of selected option (0-based), or -1 on escape
    static int select(const std::vector<std::string>& options, int defaultIdx = 0) {
#ifdef _WIN32
        int current = defaultIdx;
        if (current < 0 || current >= (int)options.size()) current = 0;

        // If stdin is not a real console (piped input), return default immediately
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        if (!GetConsoleMode(hStdin, &mode)) {
            render(options, current);
            printf("\n");
            return current;
        }

        while (true) {
            render(options, current);

            int ch = _getch();
            if (ch == 13) { // Enter
                printf("\n");
                return current;
            } else if (ch == 27) { // Escape
                printf("\n");
                return -1;
            } else if (ch == 0 || ch == 224) {
                int arrow = _getch();
                if (arrow == 75) { // Left
                    current = (current - 1 + (int)options.size()) % (int)options.size();
                } else if (arrow == 77) { // Right
                    current = (current + 1) % (int)options.size();
                }
            } else if (ch == 'y' || ch == 'Y') { // Quick key: y = Allow
                printf("\n");
                return 0;
            } else if (ch == 'n' || ch == 'N') { // Quick key: n = Deny
                printf("\n");
                return 1;
            } else if (ch == 'a' || ch == 'A') { // Quick key: a = Allow All
                printf("\n");
                return 2;
            }
        }
#else
        render(options, defaultIdx);
        printf("\n");
        return defaultIdx;
#endif
    }

private:
    static void render(const std::vector<std::string>& options, int selected) {
        printf("\r");
        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) {
                // Highlighted: yellow with arrow indicator
                printf("\033[33m[\xe2\x96\xb8 %s]\033[0m ", options[i].c_str());
            } else {
                printf("[ %s ] ", options[i].c_str());
            }
        }
        // Clear any trailing characters from previous render
        printf("   ");
        fflush(stdout);
    }
};

} // namespace closecrab
