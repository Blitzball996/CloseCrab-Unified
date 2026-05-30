#pragma once

// DiffRender — compact line-level diff for showing what an Edit/Write changed,
// translated from JackProAi's structuredPatch + StructuredDiff behavior (it uses
// the `diff` npm lib's structuredPatch; C++ has no equivalent, so this is a small
// LCS-based line differ producing the same kind of hunk: changed lines with a few
// lines of surrounding context). Behavior-equivalent, not byte-identical.
//
// Output is a JSON array of {op, text}: op is " " (context), "-" (removed),
// "+" (added). Only emits hunks around changes (with kContext lines of context),
// and caps total emitted lines so a huge rewrite doesn't flood the terminal.

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace closecrab {

class DiffRender {
public:
    static constexpr int kContext = 3;     // lines of context around each change
    static constexpr int kMaxLines = 200;  // cap on emitted diff lines

    // Build a compact diff (old -> new) as a JSON array of {op, text}.
    static nlohmann::json build(const std::string& oldText, const std::string& newText) {
        std::vector<std::string> a = splitLines(oldText);
        std::vector<std::string> b = splitLines(newText);

        // LCS table (line-level). Sizes are small for typical edits; guard huge.
        const size_t n = a.size(), m = b.size();
        nlohmann::json out = nlohmann::json::array();
        if (n + m == 0) return out;
        // Cheap guard: if files are enormous, fall back to a header-only summary.
        if (n > 5000 || m > 5000) {
            out.push_back({{"op", " "}, {"text",
                "[large change: " + std::to_string(n) + " -> " +
                std::to_string(m) + " lines]"}});
            return out;
        }

        // LCS DP
        std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
        for (size_t i = n; i-- > 0;) {
            for (size_t j = m; j-- > 0;) {
                if (a[i] == b[j]) dp[i][j] = dp[i + 1][j + 1] + 1;
                else dp[i][j] = std::max(dp[i + 1][j], dp[i][j + 1]);
            }
        }

        // Backtrack into a unified op list.
        struct Op { char op; std::string text; };
        std::vector<Op> ops;
        size_t i = 0, j = 0;
        while (i < n && j < m) {
            if (a[i] == b[j]) { ops.push_back({' ', a[i]}); i++; j++; }
            else if (dp[i + 1][j] >= dp[i][j + 1]) { ops.push_back({'-', a[i]}); i++; }
            else { ops.push_back({'+', b[j]}); j++; }
        }
        while (i < n) ops.push_back({'-', a[i++]});
        while (j < m) ops.push_back({'+', b[j++]});

        // Mark which ops are "near a change" (within kContext of a +/- op) so we
        // only print hunks, not the whole file.
        std::vector<bool> keep(ops.size(), false);
        for (size_t k = 0; k < ops.size(); k++) {
            if (ops[k].op != ' ') {
                size_t lo = (k > (size_t)kContext) ? k - kContext : 0;
                size_t hi = std::min(ops.size() - 1, k + kContext);
                for (size_t x = lo; x <= hi; x++) keep[x] = true;
            }
        }

        int emitted = 0;
        bool gap = false;
        for (size_t k = 0; k < ops.size(); k++) {
            if (!keep[k]) { gap = true; continue; }
            if (gap && emitted > 0) {
                out.push_back({{"op", " "}, {"text", "..."}});
            }
            gap = false;
            out.push_back({{"op", std::string(1, ops[k].op)}, {"text", ops[k].text}});
            if (++emitted >= kMaxLines) {
                out.push_back({{"op", " "}, {"text", "[diff truncated]"}});
                break;
            }
        }
        return out;
    }

private:
    static std::vector<std::string> splitLines(const std::string& s) {
        std::vector<std::string> lines;
        std::string cur;
        for (char c : s) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(cur);
        return lines;
    }
};

} // namespace closecrab
