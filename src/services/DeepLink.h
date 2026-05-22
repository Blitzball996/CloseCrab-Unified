#pragma once
#include <string>
#include <map>
#include <spdlog/spdlog.h>

namespace closecrab {

struct DeepLinkAction {
    std::string type;       // "open", "run", "search", "resume"
    std::string target;     // file path, command, query, session id
    std::map<std::string, std::string> params;
};

class DeepLink {
public:
    // Parse a closecrab:// URL into an action
    // Examples:
    //   closecrab://open?file=src/main.cpp&line=42
    //   closecrab://run?cmd=npm+test
    //   closecrab://search?q=login+bug
    //   closecrab://resume?session=user_123456
    static DeepLinkAction parse(const std::string& url) {
        DeepLinkAction action;

        // Check scheme
        const std::string scheme = "closecrab://";
        if (url.substr(0, scheme.size()) != scheme) return action;

        std::string rest = url.substr(scheme.size());

        // Extract action type (before ?)
        size_t qmark = rest.find('?');
        action.type = rest.substr(0, qmark);

        // Parse query params
        if (qmark != std::string::npos) {
            std::string query = rest.substr(qmark + 1);
            parseQueryString(query, action.params);
        }

        // Set target based on type
        if (action.type == "open") action.target = action.params["file"];
        else if (action.type == "run") action.target = urlDecode(action.params["cmd"]);
        else if (action.type == "search") action.target = urlDecode(action.params["q"]);
        else if (action.type == "resume") action.target = action.params["session"];

        return action;
    }

    // Register as URL handler on Windows
    static bool registerUrlScheme() {
#ifdef _WIN32
        // Would write to HKEY_CURRENT_USER\Software\Classes\closecrab
        // For now just log that it should be registered
        spdlog::info("To register closecrab:// URL scheme, run installer or:");
        spdlog::info("  reg add HKCU\\Software\\Classes\\closecrab /ve /d \"URL:CloseCrab Protocol\"");
        spdlog::info("  reg add HKCU\\Software\\Classes\\closecrab /v \"URL Protocol\" /d \"\"");
        spdlog::info("  reg add HKCU\\Software\\Classes\\closecrab\\shell\\open\\command /ve /d \"\\\"<exe_path>\\\" --deeplink \\\"%1\\\"\"");
        return true;
#else
        return false;
#endif
    }

private:
    static void parseQueryString(const std::string& query,
                                  std::map<std::string, std::string>& params) {
        size_t pos = 0;
        while (pos < query.size()) {
            size_t amp = query.find('&', pos);
            if (amp == std::string::npos) amp = query.size();

            std::string pair = query.substr(pos, amp - pos);
            size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                params[pair.substr(0, eq)] = pair.substr(eq + 1);
            }
            pos = amp + 1;
        }
    }

    static std::string urlDecode(const std::string& encoded) {
        std::string decoded;
        for (size_t i = 0; i < encoded.size(); i++) {
            if (encoded[i] == '+') decoded += ' ';
            else if (encoded[i] == '%' && i + 2 < encoded.size()) {
                int val = 0;
                sscanf(encoded.substr(i+1, 2).c_str(), "%x", &val);
                decoded += (char)val;
                i += 2;
            } else {
                decoded += encoded[i];
            }
        }
        return decoded;
    }
};

} // namespace closecrab
