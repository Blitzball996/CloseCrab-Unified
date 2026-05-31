// LicenseGate — CloseCrab-Unified's licensing shim around the shared LicenseCore.
//
// Responsibilities (the parts that differ per app):
//   - HTTP activation/verify via libcurl (honours the clash/HTTP proxy)
//   - CLI startup gate: activated -> run; trial -> countdown then lock; locked -> refuse
//   - --activate / --deactivate / --license-status entry points (driven from main.cpp)
//
// The security-critical bits (Ed25519 verify, serial checksum, device id, storage)
// live in LicenseCore (shared verbatim with AIDAW).
#pragma once

#include <string>
#include "LicenseCore.h"

namespace closecrab {

struct ActivateResult {
    bool        ok = false;
    std::string errorCode;  // OK | NOT_FOUND | ALREADY_ACTIVATED | REVOKED | WRONG_PRODUCT | NETWORK | ...
    std::string message;    // human-readable (Chinese) summary
    std::string edition;    // standard | pro (on success)
};

class LicenseGate {
public:
    static constexpr const char* kAppKey  = "CloseCrab";  // registry subkey
    static constexpr const char* kPrefix2 = "CC";         // accept only CC** serials
    static constexpr const char* kAppVersion = "0.2.0";

    // Resolved activation server base URL (env CC_LICENSE_URL > default).
    static std::string baseUrl();
    // Resolved outbound proxy (env CC_LICENSE_PROXY > ALL_PROXY > HTTPS_PROXY > "").
    static std::string proxy();

    static lic::Status status();             // evaluate stored activation / trial
    static ActivateResult activate(const std::string& rawKey);  // online activate + persist
    static void deactivate();                // clear local activation (for support/testing)

    // Print a one-line status summary to stdout.
    static void printStatus();

    // Startup gate. Prints banners; for trial, spawns a detached countdown thread
    // that locks the process when the allowance elapses. Returns true to proceed,
    // false if the app should exit immediately (trial exhausted, not activated).
    static bool enforceAtStartup();
};

} // namespace closecrab
