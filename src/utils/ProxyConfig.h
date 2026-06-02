#pragma once
// Shared HTTP proxy resolution for ALL outbound curl requests.
//
// PROBLEM this fixes: only RemoteAPIClient and LicenseGate honored the proxy
// setting. WebSearch/WebFetch/UpdateChecker/Bridge/OpenAI-compat did NOT — so on
// a network behind the GFW (or any corporate proxy), those features time out
// (e.g. api.duckduckgo.com is blocked in mainland China → "Search failed:
// Timeout was reached"). Centralizing it here means every curl call can opt in
// with one line and they all behave consistently.
//
// Priority: CLOSECRAB_PROXY > https_proxy > HTTPS_PROXY > http_proxy > HTTP_PROXY
//           > config.yaml `proxy:`. Set CLOSECRAB_NO_PROXY=1 to force-disable.
#include <curl/curl.h>
#include <string>
#include <cstdlib>
#include "../config/Config.h"

namespace closecrab {

inline bool proxyForceDisabled() {
    const char* v = std::getenv("CLOSECRAB_NO_PROXY");
    return v && (std::string(v) == "1" || std::string(v) == "true");
}

inline std::string resolveProxyUrl() {
    if (proxyForceDisabled()) return "";
    const char* envVars[] = {
        "CLOSECRAB_PROXY", "https_proxy", "HTTPS_PROXY", "http_proxy", "HTTP_PROXY"
    };
    for (const char* var : envVars) {
        const char* val = std::getenv(var);
        if (val && val[0] != '\0') return val;
    }
    // Fallback: config.yaml `proxy:`
    std::string cfgProxy = Config::getInstance().getString("proxy", "");
    if (!cfgProxy.empty()) return cfgProxy;
    return "";
}

// Apply the resolved proxy (or an explicit direct connection) to a CURL handle.
// Call right before curl_easy_perform. Safe to call on every request.
inline void applyProxyToCurl(CURL* curl) {
    if (!curl) return;
    if (proxyForceDisabled()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, "");      // ignore env-inherited proxy
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        return;
    }
    std::string proxy = resolveProxyUrl();
    if (!proxy.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
    }
}

} // namespace closecrab
