#pragma once

#include "../Tool.h"
#include "../../lsp/LSPClient.h"

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
        std::string uri = "file:///" + filePath;
        int line = input.value("line", 0);
        int character = input.value("character", 0);

        // LSP client would need to be initialized per language server
        // This is a placeholder that returns the action description
        return ToolResult::ok("[LSP " + action + "] " + filePath + ":" +
                              std::to_string(line) + ":" + std::to_string(character) +
                              "\n(LSP server not connected — use /mcp to configure)");
    }
};

} // namespace closecrab
