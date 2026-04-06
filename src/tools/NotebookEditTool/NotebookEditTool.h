#pragma once

#include "../Tool.h"
#include <fstream>
#include <filesystem>

namespace closecrab {

class NotebookEditTool : public Tool {
public:
    std::string getName() const override { return "NotebookEdit"; }
    std::string getDescription() const override {
        return "Edit a Jupyter notebook (.ipynb) cell. Supports replace, insert, and delete.";
    }
    std::string getCategory() const override { return "file"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"notebook_path",{{"type","string"},{"description","Absolute path to .ipynb file"}}},
            {"cell_number",{{"type","integer"},{"description","0-indexed cell number"}}},
            {"new_source",{{"type","string"},{"description","New cell content"}}},
            {"cell_type",{{"type","string"},{"description","code or markdown"}}},
            {"edit_mode",{{"type","string"},{"description","replace, insert, or delete"}}}
        }},{"required",{"notebook_path","new_source"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string path = input["notebook_path"].get<std::string>();
        std::string newSource = input["new_source"].get<std::string>();
        std::string editMode = input.value("edit_mode", "replace");
        int cellNum = input.value("cell_number", 0);
        std::string cellType = input.value("cell_type", "code");

        // Read notebook
        std::ifstream inFile(path);
        if (!inFile.is_open()) return ToolResult::fail("Cannot open: " + path);
        nlohmann::json nb;
        try { nb = nlohmann::json::parse(inFile); } catch (const std::exception& e) {
            return ToolResult::fail("Invalid notebook JSON: " + std::string(e.what()));
        }
        inFile.close();

        if (!nb.contains("cells") || !nb["cells"].is_array()) {
            return ToolResult::fail("No cells array in notebook");
        }

        auto& cells = nb["cells"];

        // Split source into lines
        nlohmann::json sourceLines = nlohmann::json::array();
        std::istringstream iss(newSource);
        std::string line;
        while (std::getline(iss, line)) sourceLines.push_back(line + "\n");

        if (editMode == "delete") {
            if (cellNum < 0 || cellNum >= (int)cells.size())
                return ToolResult::fail("Cell index out of range");
            cells.erase(cells.begin() + cellNum);
        } else if (editMode == "insert") {
            nlohmann::json newCell = {
                {"cell_type", cellType},
                {"source", sourceLines},
                {"metadata", nlohmann::json::object()},
                {"outputs", nlohmann::json::array()}
            };
            if (cellNum >= (int)cells.size()) cells.push_back(newCell);
            else cells.insert(cells.begin() + cellNum, newCell);
        } else { // replace
            if (cellNum < 0 || cellNum >= (int)cells.size())
                return ToolResult::fail("Cell index out of range");
            cells[cellNum]["source"] = sourceLines;
            if (!cellType.empty()) cells[cellNum]["cell_type"] = cellType;
        }

        // Write back
        std::ofstream outFile(path);
        outFile << nb.dump(1);
        return ToolResult::ok("Notebook updated: " + editMode + " cell " + std::to_string(cellNum));
    }
};

} // namespace closecrab
