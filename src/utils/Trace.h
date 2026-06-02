#pragma once
// Lightweight trace-log gate. The codebase sprinkles
//   FILE* t = fopen("trace.log","a"); if(t){ ... fclose(t); }
// for crash diagnostics. That was useful during the 闪退 investigation but writes
// on every tool call / request, growing trace.log unbounded in the app data dir.
//
// traceOpen() returns nullptr UNLESS CLOSECRAB_TRACE=1 (or true) is set, so all
// existing `if(t)` guards become no-ops by default — zero disk churn — while the
// trace can still be re-enabled on demand with one env var.
#include <cstdio>
#include <cstdlib>
#include <string>

namespace closecrab {

inline bool traceEnabled() {
    static const bool enabled = [] {
        const char* e = std::getenv("CLOSECRAB_TRACE");
        return e && (std::string(e) == "1" || std::string(e) == "true");
    }();
    return enabled;
}

// Drop-in replacement for fopen("trace.log","a"): nullptr when tracing is off.
inline std::FILE* traceOpen() {
    if (!traceEnabled()) return nullptr;
    return std::fopen("trace.log", "a");
}

} // namespace closecrab
