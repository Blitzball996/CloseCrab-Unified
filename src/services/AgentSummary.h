#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <algorithm>

namespace closecrab {

class AgentSummary {
public:
    struct ActionRecord {
        std::string toolName;
        std::string target;     // file path or command
        bool success = true;
    };

    // Generate summary from agent's action history
    static std::string summarize(const std::string& agentType,
                                  const std::vector<ActionRecord>& actions,
                                  const std::string& finalOutput) {
        std::ostringstream ss;
        ss << "Agent (" << agentType << ") completed:\n";

        // Count actions by type
        std::map<std::string, int> counts;
        std::vector<std::string> files;
        int errors = 0;

        for (const auto& a : actions) {
            counts[a.toolName]++;
            if (!a.success) errors++;
            if ((a.toolName == "Write" || a.toolName == "Edit" || a.toolName == "Read")
                && !a.target.empty()) {
                if (std::find(files.begin(), files.end(), a.target) == files.end()) {
                    files.push_back(a.target);
                }
            }
        }

        // Action summary
        ss << "  Actions: ";
        bool first = true;
        for (const auto& [tool, count] : counts) {
            if (!first) ss << ", ";
            ss << tool << "(" << count << ")";
            first = false;
        }
        ss << "\n";

        // Files touched
        if (!files.empty()) {
            ss << "  Files: ";
            for (size_t i = 0; i < std::min(files.size(), (size_t)5); i++) {
                if (i > 0) ss << ", ";
                // Show just filename
                size_t slash = files[i].find_last_of("/\\");
                ss << (slash != std::string::npos ? files[i].substr(slash+1) : files[i]);
            }
            if (files.size() > 5) ss << " (+" << (files.size()-5) << " more)";
            ss << "\n";
        }

        if (errors > 0) {
            ss << "  Errors: " << errors << "\n";
        }

        // Final output preview (first 200 chars)
        if (!finalOutput.empty()) {
            std::string preview = finalOutput.substr(0, 200);
            if (finalOutput.size() > 200) preview += "...";
            ss << "  Result: " << preview << "\n";
        }

        return ss.str();
    }
};

} // namespace closecrab
