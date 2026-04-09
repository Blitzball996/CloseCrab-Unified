#pragma once
#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace closecrab {

class MCPElicitationHandler {
public:
    // Handle MCP elicitation: collect required parameters interactively
    static nlohmann::json handleElicitation(
        const nlohmann::json& schema,
        std::function<std::string(const std::string&)> askUser)
    {
        nlohmann::json result = nlohmann::json::object();
        if (!schema.contains("properties") || !schema["properties"].is_object()) return result;

        // Determine required fields
        std::vector<std::string> required;
        if (schema.contains("required") && schema["required"].is_array()) {
            for (const auto& r : schema["required"]) required.push_back(r.get<std::string>());
        }

        for (auto& [key, prop] : schema["properties"].items()) {
            std::string type = prop.value("type", "string");
            std::string desc = prop.value("description", key);
            bool isRequired = std::find(required.begin(), required.end(), key) != required.end();

            // Build prompt
            std::string prompt = desc;
            if (isRequired) prompt += " (required)";
            if (prop.contains("default")) {
                prompt += " [default: " + prop["default"].dump() + "]";
            }
            if (prop.contains("enum") && prop["enum"].is_array()) {
                prompt += " Options: ";
                for (const auto& e : prop["enum"]) prompt += e.dump() + " ";
            }
            prompt += ": ";

            std::string value = askUser(prompt);

            // Use default if empty
            if (value.empty() && prop.contains("default")) {
                result[key] = prop["default"];
                continue;
            }
            if (value.empty() && !isRequired) continue;

            // Type conversion
            if (type == "integer" || type == "number") {
                try { result[key] = std::stod(value); } catch (...) { result[key] = value; }
            } else if (type == "boolean") {
                result[key] = (value == "true" || value == "yes" || value == "1");
            } else if (type == "array") {
                // Split by comma
                nlohmann::json arr = nlohmann::json::array();
                std::string item;
                for (char c : value) {
                    if (c == ',') { if (!item.empty()) { arr.push_back(item); item.clear(); } }
                    else item += c;
                }
                if (!item.empty()) arr.push_back(item);
                result[key] = arr;
            } else {
                result[key] = value;
            }
        }
        return result;
    }
};

} // namespace closecrab
