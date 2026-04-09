#pragma once
#include "../Tool.h"
#include "../../lsp/LSPServerManager.h"

namespace closecrab {

class LSPTool : public Tool {
public:
    std::string getName() const override { return "LSP"; }
    std::string getDescription() const override {
        return "Perform LSP operations: diagnostics, hover, definition, references.";
    }
    std::string getCategory() const override { return "code"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"action",{{"type","string"},{"description","diagnostics, hover, definition, references"}}},
            {"file_path",{{"type","string"}}},
            {"line",{{"type","integer"}}},
            {"character",{{"type","integer"}}}
        }},{"required",{"action","file_path"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string action = input["action"].get<std::string>();
        std::string filePath = input["file_path"].get<std::string>();
        int line = input.value("line", 0);
        int character = input.value("character", 0);

        // Resolve relative path
        std::string fullPath = filePath;
        if (!filePath.empty() && filePath[0] != '/' && filePath[1] != ':') {
            fullPath = ctx.cwd + "/" + filePath;
        }
        std::string uri = "file:///" + fullPath;

        auto& mgr = LSPServerManager::getInstance();
        LSPClient* client = mgr.getClientForFile(fullPath);

        if (!client || !client->isRunning()) {
            // List available servers for helpful error
            auto servers = mgr.getServerNames();
            std::string hint = servers.empty()
                ? "No LSP servers configured. Add servers in settings.json under 'lspServers'."
                : "Available servers: ";
            if (!servers.empty()) {
                for (const auto& s : servers) hint += s + " ";
            }
            return ToolResult::fail("No LSP server for " + fullPath + ". " + hint);
        }

        // Notify LSP about the file
        std::string ext = fullPath.substr(fullPath.rfind('.') + 1);
        std::string langId = ext == "py" ? "python" : ext == "ts" ? "typescript" : ext == "js" ? "javascript" : ext == "cpp" ? "cpp" : ext;
        client->didOpen(uri, langId, "");

        if (action == "diagnostics") {
            auto diags = client->diagnostics(uri);
            if (diags.empty()) return ToolResult::ok("No diagnostics for " + filePath);
            std::string result;
            for (const auto& d : diags) {
                int dLine = 0, dChar = 0;
                std::string msg, severity = "info";
                if (d.contains("range") && d["range"].contains("start")) {
                    dLine = d["range"]["start"].value("line", 0);
                    dChar = d["range"]["start"].value("character", 0);
                }
                msg = d.value("message", "");
                int sev = d.value("severity", 4);
                if (sev == 1) severity = "error";
                else if (sev == 2) severity = "warning";
                else if (sev == 3) severity = "info";
                result += filePath + ":" + std::to_string(dLine + 1) + ":" + std::to_string(dChar + 1)
                       + " [" + severity + "] " + msg + "\n";
            }
            return ToolResult::ok(result);

        } else if (action == "hover") {
            auto hover = client->hover(uri, line, character);
            if (hover.is_null() || hover.empty()) return ToolResult::ok("No hover info at this position.");
            std::string content;
            if (hover.contains("contents")) {
                auto& c = hover["contents"];
                if (c.is_string()) content = c.get<std::string>();
                else if (c.is_object() && c.contains("value")) content = c["value"].get<std::string>();
                else content = c.dump(2);
            }
            return ToolResult::ok(content);

        } else if (action == "definition") {
            auto def = client->definition(uri, line, character);
            if (def.is_null() || def.empty()) return ToolResult::ok("No definition found.");
            std::string result;
            auto formatLoc = [](const nlohmann::json& loc) -> std::string {
                std::string u = loc.value("uri", "");
                int l = 0, c = 0;
                if (loc.contains("range") && loc["range"].contains("start")) {
                    l = loc["range"]["start"].value("line", 0);
                    c = loc["range"]["start"].value("character", 0);
                }
                return u + ":" + std::to_string(l + 1) + ":" + std::to_string(c + 1);
            };
            if (def.is_array()) {
                for (const auto& d : def) result += formatLoc(d) + "\n";
            } else {
                result = formatLoc(def);
            }
            return ToolResult::ok(result);

        } else if (action == "references") {
            auto refs = client->references(uri, line, character);
            if (refs.is_null() || refs.empty()) return ToolResult::ok("No references found.");
            std::string result;
            if (refs.is_array()) {
                result = std::to_string(refs.size()) + " references found:\n";
                for (const auto& r : refs) {
                    std::string u = r.value("uri", "");
                    int l = 0;
                    if (r.contains("range") && r["range"].contains("start"))
                        l = r["range"]["start"].value("line", 0);
                    result += u + ":" + std::to_string(l + 1) + "\n";
                }
            }
            return ToolResult::ok(result);
        }

        return ToolResult::fail("Unknown LSP action: " + action + ". Use diagnostics, hover, definition, or references.");
    }
};

} // namespace closecrab
