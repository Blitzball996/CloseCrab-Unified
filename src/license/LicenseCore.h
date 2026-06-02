// LicenseCore — shared, dependency-free licensing core for Blitzball Labs apps.
//
// Implements the serial-number format + online-activation client logic from
// docs/序列号格式规范.md (CompanyWebsite-Backend). The SAME file is vendored
// verbatim into both CloseCrab-Unified and AIDAW so the two apps agree on:
//   - serial parsing / Crockford-Base32 checksum (spec §2/§4/§6)
//   - device fingerprint (spec §7)
//   - Ed25519 token verification (spec §5) via vendored TweetNaCl
//   - local persistence (Windows registry) of the activation token + trial state
//
// It has NO external dependencies (no curl, no nlohmann, no JUCE) — each app
// supplies the HTTP transport + UI around it. See LicenseGate (CloseCrab) and
// LicenseManager (AIDAW).
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace lic {

// --- Embedded Ed25519 PUBLIC key (32 bytes). -------------------------------
// Matches the server signing key (base64url: cyKUWoytmoNBZ9g1Oehr-xfV9YXdjVTE4qzrIDfzcqc).
// The matching PRIVATE seed lives ONLY on the backend (LICENSE_PRIVATE_KEY).
extern const unsigned char kPublicKey[32];

// Default activation server (override via env / config in each app).
inline const char* kDefaultBaseUrl() { return "https://blitzball.lol"; }

// --- Serial parsing (spec §2/§4/§6) ----------------------------------------
struct ParseResult {
    bool        ok = false;
    std::string canonical;   // PROD-GGGG-GGGG-GGGG-GGGG
    std::string product;     // BDST/BDPR/CCST/CCPR
    std::string errCode;     // "" | BAD_FORMAT | BAD_CHECKSUM | WRONG_PRODUCT
};
std::string normalizeKey(const std::string& raw);
// expectedPrefix2: "CC" (CloseCrab) or "BD" (Blitz DAW / AIDAW); ""=accept any.
ParseResult parseKey(const std::string& raw, const std::string& expectedPrefix2 = "");

// --- Crypto / encoding primitives -------------------------------------------
std::string sha256Hex(const std::string& data);                  // 64-hex lowercase
bool        base64Decode(const std::string& in, std::vector<unsigned char>& out); // url+std, padless ok
bool        ed25519Verify(const unsigned char pub[32],
                          const unsigned char* msg, size_t msgLen,
                          const unsigned char sig[64]);

// Minimal flat-JSON string field extractor for the (signed, server-controlled)
// activation token, e.g. jsonGetString(json, "device_id").
std::string jsonGetString(const std::string& json, const std::string& key);

// --- Device fingerprint (spec §7) ------------------------------------------
// Stable per-machine id derived from the OS machine GUID; 32 hex chars.
std::string deviceId();

// --- Token verification (spec §5) ------------------------------------------
struct TokenInfo {
    bool        ok = false;
    std::string key, product, edition, deviceId;
};
// Verifies signature with kPublicKey, then checks device_id==expectedDeviceId
// and product starts with expectedPrefix2.
TokenInfo verifyToken(const std::string& tokenB64, const std::string& sigB64,
                      const std::string& expectedDeviceId,
                      const std::string& expectedPrefix2);

// --- Persistent state (HKCU\Software\Blitzball\<appKey> on Windows) ---------
struct StoredActivation {
    bool        present = false;
    std::string key, tokenB64, sigB64, edition;
};
StoredActivation loadActivation(const std::string& appKey);
bool saveActivation(const std::string& appKey, const std::string& key,
                    const std::string& tokenB64, const std::string& sigB64,
                    const std::string& edition);
void clearActivation(const std::string& appKey);  // for testing / deactivation

int  getTrialRuns(const std::string& appKey);            // committed completed-run count
void setTrialRuns(const std::string& appKey, int runs);

// --- High-level evaluation (rule A: 30min / 10min / locked) ----------------
enum class State { Activated, Trial, Locked };
struct Status {
    State       state = State::Locked;
    std::string edition;          // when Activated
    std::string key;              // when Activated
    int         trialRun = 0;     // prospective run #: 1,2,3...
    int         allowanceSeconds = 0; // this run's allowance (1800 / 600 / 0)
};
// allowance schedule for a given prospective run number (1-based).
int allowanceForRun(int run);
// Reads stored activation, verifies it; if valid -> Activated. Otherwise
// computes the prospective trial run (committedRuns+1) WITHOUT committing it.
Status evaluate(const std::string& appKey, const std::string& expectedPrefix2);
// Commit a trial run once it has actually been used (>60s). Idempotent per run:
// only raises the stored counter to `run` if it is currently lower.
void commitTrialRun(const std::string& appKey, int run);

} // namespace lic
