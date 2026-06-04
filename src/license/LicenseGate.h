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
#ifdef CLOSECRAB_VERSION
    static constexpr const char* kAppVersion = CLOSECRAB_VERSION;
#else
    static constexpr const char* kAppVersion = "0.0.0";
#endif

    // Resolved primary activation server base URL. Priority: first entry in
    // CC_LICENSE_URLS (comma/semicolon/whitespace-separated mirrors) >
    // CC_LICENSE_URL > default. activate() tries ALL resolved mirrors.
    static std::string baseUrl();
    // Resolved outbound proxy. Priority: saved ProxyUrl (registry, via setProxy)
    // > env CC_LICENSE_PROXY > ALL_PROXY > HTTPS_PROXY > "". Empty means "auto":
    // activate() also auto-tries a direct connection + common clash/v2ray ports.
    static std::string proxy();
    // Persist a user-supplied proxy (e.g. "http://127.0.0.1:7897") so future
    // activations use it first. Pass "" to clear.
    static void setProxy(const std::string& url);

    static lic::Status status();             // evaluate stored activation / trial
    static ActivateResult activate(const std::string& rawKey);  // online activate + persist
    static void deactivate();                // clear local activation (for support/testing)

    // Offline activation: when the machine can't reach the server, the user gets
    // their signed token elsewhere (web page / another device) keyed to THIS
    // machine's Device ID, and pastes it back as "token|sig|edition". We verify
    // the Ed25519 signature against the embedded public key (no network) and, if
    // it matches this device, persist it — same trust path as online activation.
    static ActivateResult activateOffline(const std::string& blob);

    // The Device ID the user must give the activation page to mint an offline
    // token (== lic::deviceId()). Exposed so the gate UI can print it.
    static std::string deviceId();

    // Print a one-line status summary to stdout.
    static void printStatus();

    // Startup gate. Prints banners; for trial, spawns a detached countdown thread
    // that locks the process when the allowance elapses. Returns true to proceed,
    // false if the app should exit immediately (trial exhausted, not activated).
    static bool enforceAtStartup();
};

} // namespace closecrab
