#pragma once

#include "../Tool.h"
#include "../ToolRegistry.h"
#include <algorithm>
#include <sstream>
#include <regex>

namespace closecrab {

// JackProAi-style ToolSearchTool: model calls this to "discover" deferred tools.
// Deferred tools (shouldDefer()=true) are not sent with full schema initially —
// the model sees only their names, then calls ToolSearch with a query to load
// the schema for the next turn. This keeps the API request body small enough
// for proxies that fail with 18+ tools while exposing all tool capabilities.
class ToolSearchTool : public Tool {
public:
    std::string getName() const override { return "ToolSearch"; }
    std::string getDescription() const override {
        return "Fetches full schema definitions for deferred tools so they can be called. "
               "Until fetched, only the tool name is known — there is no parameter schema. "
               "Query forms:\n"
               "  \"select:Read,Edit,Grep\" — fetch these exact tools by name\n"
               "  \"file search\" — keyword search across deferred tools";
    }
    std::string getCategory() const override { return "meta"; }
    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe() const override { return true; }
    bool shouldDefer() const override { return false; }  // ToolSearch itself NEVER deferred
    bool alwaysLoad() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description",
                    "Use \"select:<tool_name>\" for direct selection, or keywords to search."}}},
                {"max_results", {{"type", "integer"},
                    {"description", "Max number of results (default 5)"}}}
            }},
            {"required", {"query"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        if (!ctx.toolRegistry) {
            return ToolResult::fail("Tool registry unavailable");
        }
        std::string query = input.value("query", "");
        int maxResults = input.value("max_results", 5);
        if (query.empty()) {
            return ToolResult::fail("query is required");
        }

        // Collect deferred tools
        std::vector<Tool*> deferredTools;
        for (Tool* t : ctx.toolRegistry->getAllTools()) {
            if (!t || !t->isEnabled() || t->isHidden()) continue;
            if (t->shouldDefer()) deferredTools.push_back(t);
        }

        std::vector<std::string> matches;

        // select:<name1>,<name2> direct selection
        if (query.size() > 7 && query.substr(0, 7) == "select:") {
            std::string list = query.substr(7);
            std::stringstream ss(list);
            std::string item;
            while (std::getline(ss, item, ',')) {
                // Trim
                while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.erase(0, 1);
                while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.pop_back();
                if (item.empty()) continue;
                // Find by name (case-insensitive) in deferred OR full tool list
                Tool* found = nullptr;
                for (Tool* t : deferredTools) {
                    if (caseInsensitiveEqual(t->getName(), item)) { found = t; break; }
                }
                if (!found) {
                    for (Tool* t : ctx.toolRegistry->getAllTools()) {
                        if (t && caseInsensitiveEqual(t->getName(), item)) { found = t; break; }
                    }
                }
                if (found) matches.push_back(found->getName());
            }
        } else {
            // Keyword search across deferred tool names + descriptions
            std::string queryLower = toLower(query);
            std::vector<std::pair<int, std::string>> scored;
            for (Tool* t : deferredTools) {
                int score = scoreToolMatch(t, queryLower);
                if (score > 0) scored.push_back({score, t->getName()});
            }
            std::sort(scored.begin(), scored.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < scored.size() && (int)i < maxResults; i++) {
                matches.push_back(scored[i].second);
            }
        }

        // Build result — list of tool_reference markers that QueryEngine recognizes
        // as "this tool is now discovered" via parseDiscoveredTools().
        nlohmann::json result;
        result["matches"] = matches;
        result["query"] = query;
        result["total_deferred_tools"] = deferredTools.size();

        // Build human-readable content. Include marker that QueryEngine will scan
        // to extract discovered tool names from message history.
        std::string content;
        if (matches.empty()) {
            content = "No matches found for query: " + query +
                "\nTotal deferred tools available: " + std::to_string(deferredTools.size());
        } else {
            content = "Discovered " + std::to_string(matches.size()) + " tool(s):\n";
            for (const auto& name : matches) {
                content += "<tool_reference name=\"" + name + "\"/>\n";
                Tool* t = ctx.toolRegistry->getTool(name);
                if (t) {
                    content += "- " + name + ": " + t->getDescription() + "\n";
                }
            }
            content += "\nThese tools are now available — call them directly in the next turn.";
        }
        return ToolResult::ok(content, result);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Search tools: " + input.value("query", "");
    }

private:
    static std::string toLower(const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return r;
    }

    static bool caseInsensitiveEqual(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) {
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
        }
        return true;
    }

    static int scoreToolMatch(Tool* tool, const std::string& queryLower) {
        std::string nameLower = toLower(tool->getName());
        std::string descLower = toLower(tool->getDescription());

        // Exact name match — highest score
        if (nameLower == queryLower) return 100;
        // Name contains query as substring
        if (nameLower.find(queryLower) != std::string::npos) return 50;
        // Description contains query
        if (descLower.find(queryLower) != std::string::npos) return 10;
        // Token match
        std::stringstream ss(queryLower);
        std::string token;
        int tokenScore = 0;
        while (std::getline(ss, token, ' ')) {
            if (token.empty()) continue;
            if (nameLower.find(token) != std::string::npos) tokenScore += 5;
            if (descLower.find(token) != std::string::npos) tokenScore += 2;
        }
        return tokenScore;
    }
};

} // namespace closecrab
