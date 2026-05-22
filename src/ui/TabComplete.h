#pragma once
#include <string>
#include <vector>
#include <algorithm>

namespace closecrab {

class TabComplete {
public:
    static std::string complete(const std::string& partial, const std::vector<std::string>& candidates) {
        if (partial.empty() || partial[0] != '/') return partial;
        std::string prefix = partial.substr(1); // remove /

        std::vector<std::string> matches;
        for (const auto& cmd : candidates) {
            if (cmd.substr(0, prefix.size()) == prefix) {
                matches.push_back(cmd);
            }
        }

        if (matches.empty()) return partial;
        if (matches.size() == 1) return "/" + matches[0] + " ";

        // Find common prefix among matches
        std::string common = matches[0];
        for (size_t i = 1; i < matches.size(); i++) {
            size_t j = 0;
            while (j < common.size() && j < matches[i].size() && common[j] == matches[i][j]) j++;
            common = common.substr(0, j);
        }
        return "/" + common;
    }

    static std::string listMatches(const std::string& partial, const std::vector<std::string>& candidates) {
        std::string prefix = partial.size() > 1 ? partial.substr(1) : "";
        std::string result;
        for (const auto& cmd : candidates) {
            if (cmd.substr(0, prefix.size()) == prefix) {
                result += "  /" + cmd + "\n";
            }
        }
        return result;
    }
};

} // namespace closecrab
