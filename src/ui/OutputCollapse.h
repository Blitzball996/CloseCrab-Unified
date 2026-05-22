#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

namespace closecrab {

class OutputCollapse {
public:
    static constexpr int MAX_DISPLAY_LINES = 15;
    static constexpr int TAIL_LINES = 5;

    struct CollapsedOutput {
        std::string display;    // What to show in terminal
        int totalLines = 0;
        bool collapsed = false;
    };

    static CollapsedOutput collapse(const std::string& output) {
        CollapsedOutput result;
        if (output.empty()) {
            result.display = "(no output)";
            return result;
        }

        // Count lines
        std::vector<std::string> lines;
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }
        result.totalLines = (int)lines.size();

        if (result.totalLines <= MAX_DISPLAY_LINES) {
            result.display = output;
            return result;
        }

        // Collapse: show first few + ... + last few
        result.collapsed = true;
        std::string collapsed;
        int headLines = MAX_DISPLAY_LINES - TAIL_LINES - 1;
        for (int i = 0; i < headLines && i < (int)lines.size(); i++) {
            collapsed += lines[i] + "\n";
        }
        collapsed += "  ... (" + std::to_string(result.totalLines - MAX_DISPLAY_LINES) + " lines hidden) ...\n";
        for (int i = std::max(0, (int)lines.size() - TAIL_LINES); i < (int)lines.size(); i++) {
            collapsed += lines[i] + "\n";
        }
        result.display = collapsed;
        return result;
    }
};

} // namespace closecrab
