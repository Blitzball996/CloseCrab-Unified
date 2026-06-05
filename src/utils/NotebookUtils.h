#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <filesystem>

namespace closecrab {

// Parse Jupyter Notebook and extract cells
// JackProAi alignment: utils/notebook.ts:readNotebook
inline nlohmann::json readNotebook(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open notebook: " + path.string());
    }

    nlohmann::json notebook;
    try {
        file >> notebook;
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid notebook JSON: " + std::string(e.what()));
    }

    // Validate notebook structure
    if (!notebook.contains("cells") || !notebook["cells"].is_array()) {
        throw std::runtime_error("Invalid notebook: missing 'cells' array");
    }

    return notebook["cells"];
}

// Format notebook cells for display (simplified version)
// JackProAi uses complex rendering, we'll do basic text format
inline std::string formatNotebookCells(const nlohmann::json& cells) {
    std::string result;
    int cellNum = 1;

    for (const auto& cell : cells) {
        std::string cellType = cell.value("cell_type", "unknown");

   result += "=== Cell " + std::to_string(cellNum++) + " [" + cellType + "] ===\n";

        // Source code/markdown
        if (cell.contains("source")) {
            auto source = cell["source"];
            if (source.is_array()) {
             for (const auto& line : source) {
                 result += line.get<std::string>();
                }
            } else if (source.is_string()) {
             result += source.get<std::string>();
       }
            result += "\n";
        }

        // Outputs (for code cells)
        if (cellType == "code" && cell.contains("outputs") && cell["outputs"].is_array()) {
            for (const auto& output : cell["outputs"]) {
                std::string outputType = output.value("output_type", "");

            if (outputType == "stream") {
                    // stdout/stderr
                    auto text = output.value("text", nlohmann::json::array());
                result += "[Output]:\n";
                  if (text.is_array()) {
                        for (const auto& line : text) {
                   result += line.get<std::string>();
                     }
                    } else if (text.is_string()) {
                     result += text.get<std::string>();
           }
                    result += "\n";
                } else if (outputType == "execute_result" || outputType == "display_data") {
                    // Check for text/plain output
                    if (output.contains("data") && output["data"].contains("text/plain")) {
                      auto text = output["data"]["text/plain"];
                 result += "[Result]:\n";
                        if (text.is_array()) {
                     for (const auto& line : text) {
                            result += line.get<std::string>();
                    }
                    } else if (text.is_string()) {
                 result += text.get<std::string>();
                        }
                        result += "\n";
                    }

              // Note about images (don't include base64)
             if (output.contains("data") &&
                (output["data"].contains("image/png") || output["data"].contains("image/jpeg"))) {
                result += "[Image output present - use ImageInput tool to view]\n";
                    }
                } else if (outputType == "error") {
             // Error traceback
               result += "[Error]:\n";
                 if (output.contains("ename")) {
                    result += output["ename"].get<std::string>() + ": ";
                    }
                    if (output.contains("evalue")) {
                      result += output["evalue"].get<std::string>() + "\n";
                    }
                if (output.contains("traceback") && output["traceback"].is_array()) {
                      for (const auto& line : output["traceback"]) {
                          result += line.get<std::string>() + "\n";
                        }
            }
            }
            }
        }

        result += "\n";
    }

    return result;
}

// Check if notebook content exceeds size limit
// JackProAi limit: 1MB serialized JSON
constexpr size_t NOTEBOOK_MAX_BYTES = 1024 * 1024;

inline bool notebookExceedsLimit(const nlohmann::json& cells) {
    std::string serialized = cells.dump();
    return serialized.size() > NOTEBOOK_MAX_BYTES;
}

// Get suggested jq commands for large notebooks
inline std::string getNotebookSizeErrorMessage(const std::string& path, size_t actualSize) {
    return "Notebook content (" + std::to_string(actualSize / 1024) + "KB) exceeds maximum allowed size (1024KB). "
           "Use bash with jq to read specific portions:\n"
        "  cat \"" + path + "\" | jq '.cells[:20]'  # First 20 cells\n"
           "  cat \"" + path + "\" | jq '.cells[100:120]'  # Cells 100-120\n"
           "  cat \"" + path + "\" | jq '.cells | length'  # Count total cells\n"
           "  cat \"" + path + "\" | jq '.cells[] | select(.cell_type==\"code\") | .source'  # All code sources";
}

} // namespace closecrab
