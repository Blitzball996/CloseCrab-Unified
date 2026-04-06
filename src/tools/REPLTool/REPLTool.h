#pragma once

#include "../Tool.h"
#include <cstdio>
#include <array>
#include <memory>
#include <thread>
#include <chrono>

namespace closecrab {

class REPLTool : public Tool {
public:
    std::string getName() const override { return "REPL"; }
    std::string getDescription() const override {
        return "Execute code in a Python or Node.js REPL and return the output.";
    }
    std::string getCategory() const override { return "execution"; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"language", {{"type", "string"}, {"description", "python or node"}}},
                {"code", {{"type", "string"}, {"description", "Code to execute"}}}
            }},
            {"required", {"code"}}
        };
    }

    bool isDestructive() const override { return true; }

    PermissionResult checkPermissions(const ToolContext&, const nlohmann::json&) const override {
        return PermissionResult::ASK_USER;
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string language = input.value("language", "python");
        std::string code = input["code"].get<std::string>();

        std::string interpreter;
        std::string flag;

        if (language == "node" || language == "javascript" || language == "js") {
            interpreter = "node";
            flag = "-e";
        } else {
            // Default to python
            interpreter = findPython();
            flag = "-c";
        }

        if (interpreter.empty()) {
            return ToolResult::fail("Interpreter not found for: " + language);
        }

        // Escape code for command line
        std::string escapedCode = escapeCode(code, language);
        std::string cmd = interpreter + " " + flag + " " + escapedCode + " 2>&1";

        std::string output;
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif
        if (!pipe) return ToolResult::fail("Failed to start " + interpreter);

        std::array<char, 4096> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            output += buffer.data();
            if (ctx.abortFlag && ctx.abortFlag->load()) break;
            if (output.size() > 100 * 1024) {
                output += "\n... (truncated)";
                break;
            }
        }

        return ToolResult::ok(output);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "REPL (" + input.value("language", "python") + ")";
    }

private:
    static std::string findPython() {
        // Try python3 first, then python
        const char* candidates[] = {"python3", "python", "py"};
        for (const char* name : candidates) {
            std::string cmd = std::string(name) + " --version 2>&1";
#ifdef _WIN32
            FILE* f = _popen(cmd.c_str(), "r");
            if (f) {
                char buf[128];
                if (fgets(buf, sizeof(buf), f) && std::string(buf).find("Python") != std::string::npos) {
                    _pclose(f);
                    return name;
                }
                _pclose(f);
            }
#else
            FILE* f = popen(cmd.c_str(), "r");
            if (f) {
                char buf[128];
                if (fgets(buf, sizeof(buf), f) && std::string(buf).find("Python") != std::string::npos) {
                    pclose(f);
                    return name;
                }
                pclose(f);
            }
#endif
        }
        return "";
    }

    static std::string escapeCode(const std::string& code, const std::string& language) {
#ifdef _WIN32
        // Windows: write to temp file and execute
        // This avoids complex escaping issues
        std::string ext = (language == "node" || language == "js") ? ".js" : ".py";
        std::string tmpFile = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") +
                              "\\closecrab_repl" + ext;
        std::ofstream f(tmpFile);
        f << code;
        f.close();
        return "\"" + tmpFile + "\"";
#else
        // Unix: use heredoc-style via -c
        std::string escaped = "\"";
        for (char c : code) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '$') escaped += "\\$";
            else if (c == '`') escaped += "\\`";
            else escaped += c;
        }
        escaped += "\"";
        return escaped;
#endif
    }
};

} // namespace closecrab
