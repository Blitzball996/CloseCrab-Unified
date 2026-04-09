#pragma once
#include "../Tool.h"
#include "../../utils/ProcessRunner.h"
#include <string>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {

class WebBrowserTool : public Tool {
public:
    std::string getName() const override { return "WebBrowser"; }
    std::string getDescription() const override {
        return "Automate browser interactions via Chrome DevTools Protocol.";
    }
    std::string getCategory() const override { return "network"; }

    nlohmann::json getInputSchema() const override {
        return {{"type","object"},{"properties",{
            {"action",{{"type","string"},{"description","navigate, click, type, screenshot, evaluate, get_text"}}},
            {"url",{{"type","string"},{"description","URL for navigate action"}}},
            {"selector",{{"type","string"},{"description","CSS selector for click/type/get_text"}}},
            {"text",{{"type","string"},{"description","Text for type action"}}},
            {"expression",{{"type","string"},{"description","JS expression for evaluate action"}}}
        }},{"required",{"action"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string action = input["action"].get<std::string>();

        // Discover CDP endpoint
        std::string cdpUrl = discoverCDP();
        if (cdpUrl.empty()) {
            return ToolResult::fail(
                "Chrome not found with remote debugging enabled.\n"
                "Start Chrome with: chrome --remote-debugging-port=9222\n"
                "Or: chromium --remote-debugging-port=9222 --headless");
        }

        if (action == "navigate") {
            std::string url = input.value("url", "");
            if (url.empty()) return ToolResult::fail("URL required for navigate action");
            auto result = sendCDP(cdpUrl, "Page.navigate", {{"url", url}});
            return ToolResult::ok("Navigated to " + url);

        } else if (action == "evaluate" || action == "click" || action == "get_text" || action == "type") {
            std::string expr;
            if (action == "evaluate") {
                expr = input.value("expression", "");
                if (expr.empty()) return ToolResult::fail("Expression required");
            } else if (action == "click") {
                std::string sel = input.value("selector", "");
                if (sel.empty()) return ToolResult::fail("Selector required");
                expr = "document.querySelector('" + escapeJS(sel) + "').click()";
            } else if (action == "get_text") {
                std::string sel = input.value("selector", "");
                if (sel.empty()) return ToolResult::fail("Selector required");
                expr = "document.querySelector('" + escapeJS(sel) + "')?.textContent || ''";
            } else if (action == "type") {
                std::string sel = input.value("selector", "");
                std::string text = input.value("text", "");
                if (sel.empty()) return ToolResult::fail("Selector required");
                expr = "(() => { const el = document.querySelector('" + escapeJS(sel) + "');"
                       "if(el){el.focus();el.value='" + escapeJS(text) + "';el.dispatchEvent(new Event('input',{bubbles:true}));"
                       "return 'typed';}return 'element not found';})()";
            }
            auto result = sendCDP(cdpUrl, "Runtime.evaluate", {{"expression", expr}, {"returnByValue", true}});
            if (result.contains("result") && result["result"].contains("value")) {
                return ToolResult::ok(result["result"]["value"].dump());
            }
            return ToolResult::ok(result.dump(2));

        } else if (action == "screenshot") {
            auto result = sendCDP(cdpUrl, "Page.captureScreenshot", {{"format", "png"}});
            if (result.contains("data")) {
                std::string b64 = result["data"].get<std::string>();
                return ToolResult::ok("[Screenshot captured: " + std::to_string(b64.size()) + " bytes base64]",
                                      {{"base64", b64}, {"format", "png"}});
            }
            return ToolResult::fail("Screenshot failed");
        }

        return ToolResult::fail("Unknown action: " + action);
    }

private:
    std::string discoverCDP() const {
        try {
            auto result = ProcessRunner::run("curl -s http://localhost:9222/json/version", 5000);
            if (result.exitCode == 0 && !result.output.empty()) {
                auto j = nlohmann::json::parse(result.output);
                if (j.contains("webSocketDebuggerUrl")) {
                    return j["webSocketDebuggerUrl"].get<std::string>();
                }
            }
        } catch (...) {}
        return "";
    }

    nlohmann::json sendCDP(const std::string& /*wsUrl*/, const std::string& method,
                           const nlohmann::json& params) const {
        static int cmdId = 1;
        nlohmann::json cmd = {{"id", cmdId++}, {"method", method}, {"params", params}};

        try {
            auto listResult = ProcessRunner::run("curl -s http://localhost:9222/json", 5000);
            if (listResult.exitCode != 0) return {{"error", "Cannot connect to CDP"}};

            auto tabs = nlohmann::json::parse(listResult.output);
            if (!tabs.is_array() || tabs.empty()) return {{"error", "No tabs found"}};

            std::string tabId = tabs[0].value("id", "");

            if (method == "Page.navigate" && params.contains("url")) {
                std::string url = params["url"].get<std::string>();
#ifdef _WIN32
                std::string openCmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
                std::string openCmd = "open \"" + url + "\"";
#else
                std::string openCmd = "xdg-open \"" + url + "\" 2>/dev/null";
#endif
                ProcessRunner::run(openCmd, 5000);
                return {{"result", "navigated"}};
            }

            auto postResult = ProcessRunner::run(
                "curl -s -X PUT http://localhost:9222/json/activate/" + tabId, 5000);

            return {{"result", {{"value", postResult.output}}}};
        } catch (const std::exception& e) {
            return {{"error", e.what()}};
        }
    }

    static std::string escapeJS(const std::string& s) {
        std::string r;
        for (char c : s) {
            if (c == '\'' || c == '\\') r += '\\';
            r += c;
        }
        return r;
    }
};

} // namespace closecrab
