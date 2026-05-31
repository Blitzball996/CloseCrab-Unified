// LicenseCore implementation — see LicenseCore.h. No external deps.
#include "LicenseCore.h"

#include <cstring>
#include <algorithm>

extern "C" {
#include "tweetnacl.h"
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lic {

// Public key bytes for base64url "dF816QXqpmv7Q2mxltJp0E2fmzyHzME0Dsn_ptjnOcc".
const unsigned char kPublicKey[32] = {
    0x74, 0x5f, 0x35, 0xe9, 0x05, 0xea, 0xa6, 0x6b, 0xfb, 0x43, 0x69, 0xb1,
    0x96, 0xd2, 0x69, 0xd0, 0x4d, 0x9f, 0x9b, 0x3c, 0x87, 0xcc, 0xc1, 0x34,
    0x0e, 0xc9, 0xff, 0xa6, 0xd8, 0xe7, 0x39, 0xc7};

// ===========================================================================
// SHA-256 (public-domain, compact)
// ===========================================================================
namespace {
struct Sha256 {
    uint32_t s[8];
    uint64_t len = 0;
    unsigned char buf[64];
    size_t n = 0;
    Sha256() {
        static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                       0xa54ff53a, 0x510e527f, 0x9b05688c,
                                       0x1f83d9ab, 0x5be0cd19};
        std::memcpy(s, iv, sizeof iv);
    }
    static uint32_t ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
    void block(const unsigned char* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4], f = s[5],
                 g = s[6], h = s[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + S1 + ch + k[i] + w[i];
            uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        s[0] += a; s[1] += b; s[2] += c; s[3] += d;
        s[4] += e; s[5] += f; s[6] += g; s[7] += h;
    }
    void update(const unsigned char* p, size_t l) {
        len += l;
        while (l) {
            size_t take = std::min(l, size_t(64) - n);
            std::memcpy(buf + n, p, take);
            n += take; p += take; l -= take;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    void final(unsigned char out[32]) {
        uint64_t bits = len * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char z = 0;
        while (n != 56) update(&z, 1);
        unsigned char lb[8];
        for (int i = 0; i < 8; i++) lb[i] = (unsigned char)(bits >> (56 - i * 8));
        update(lb, 8);
        for (int i = 0; i < 8; i++) {
            out[i * 4] = (unsigned char)(s[i] >> 24);
            out[i * 4 + 1] = (unsigned char)(s[i] >> 16);
            out[i * 4 + 2] = (unsigned char)(s[i] >> 8);
            out[i * 4 + 3] = (unsigned char)(s[i]);
        }
    }
};
} // namespace

std::string sha256Hex(const std::string& data) {
    unsigned char d[32];
    Sha256 h;
    h.update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    h.final(d);
    static const char* hx = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 32; i++) {
        out.push_back(hx[d[i] >> 4]);
        out.push_back(hx[d[i] & 0xf]);
    }
    return out;
}

// ===========================================================================
// Base64 decode (accepts standard and URL alphabets, padding optional)
// ===========================================================================
bool base64Decode(const std::string& in, std::vector<unsigned char>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    out.clear();
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        int v = val(c);
        if (v < 0) return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((unsigned char)((buf >> bits) & 0xff));
        }
    }
    return true;
}

// ===========================================================================
// Ed25519 detached verify (via TweetNaCl crypto_sign_open)
// ===========================================================================
bool ed25519Verify(const unsigned char pub[32], const unsigned char* msg,
                   size_t msgLen, const unsigned char sig[64]) {
    // TweetNaCl exposes only the combined open(): sm = sig(64) || msg.
    std::vector<unsigned char> sm(64 + msgLen);
    std::memcpy(sm.data(), sig, 64);
    if (msgLen) std::memcpy(sm.data() + 64, msg, msgLen);
    std::vector<unsigned char> m(sm.size());
    unsigned long long mlen = 0;
    int rc = crypto_sign_open(m.data(), &mlen, sm.data(),
                              (unsigned long long)sm.size(), pub);
    return rc == 0;
}

// ===========================================================================
// Serial parsing (Crockford Base32, spec §2/§4/§6)
// ===========================================================================
namespace {
const char* kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
int idxOf(char c) {
    for (int i = 0; i < 32; i++)
        if (kAlphabet[i] == c) return i;
    return -1;
}
char checksumChar(const std::string& data15) {
    int sum = 0;
    for (int i = 0; i < 15; i++) sum += idxOf(data15[i]) * (i + 1);
    return kAlphabet[((sum % 32) + 32) % 32];
}
bool isValidProduct(const std::string& p) {
    return p == "BDST" || p == "BDPR" || p == "CCST" || p == "CCPR";
}
} // namespace

std::string normalizeKey(const std::string& raw) {
    std::string s;
    for (char c : raw) {
        if (c == ' ' || c == '-' || c == '\t' || c == '\r' || c == '\n') continue;
        char u = (char)std::toupper((unsigned char)c);
        switch (u) {  // tolerant substitutions (spec §6)
            case 'I': case 'L': u = '1'; break;
            case 'O': u = '0'; break;
            case 'U': u = 'V'; break;
            default: break;
        }
        s.push_back(u);
    }
    return s;
}

ParseResult parseKey(const std::string& raw, const std::string& expectedPrefix2) {
    ParseResult r;
    std::string n = normalizeKey(raw);
    if (n.size() != 20) { r.errCode = "BAD_FORMAT"; return r; }
    std::string product = n.substr(0, 4);
    if (!isValidProduct(product)) { r.errCode = "WRONG_PRODUCT"; return r; }
    if (!expectedPrefix2.empty() && product.substr(0, 2) != expectedPrefix2) {
        r.errCode = "WRONG_PRODUCT";
        return r;
    }
    std::string body = n.substr(4);  // 16 chars
    for (char c : body)
        if (idxOf(c) < 0) { r.errCode = "BAD_FORMAT"; return r; }
    if (checksumChar(body.substr(0, 15)) != body[15]) {
        r.errCode = "BAD_CHECKSUM";
        return r;
    }
    r.ok = true;
    r.product = product;
    r.canonical = product + "-" + body.substr(0, 4) + "-" + body.substr(4, 4) +
                  "-" + body.substr(8, 4) + "-" + body.substr(12, 4);
    return r;
}

// ===========================================================================
// Minimal flat-JSON string field extractor (signed, server-controlled token)
// ===========================================================================
std::string jsonGetString(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return "";
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) p++;
    if (p >= json.size() || json[p] != '"') return "";
    p++;
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            p++;
            char e = json[p];
            switch (e) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                default: out.push_back(e); break;
            }
        } else {
            out.push_back(json[p]);
        }
        p++;
    }
    return out;
}

// ===========================================================================
// Device fingerprint (spec §7)
// ===========================================================================
std::string deviceId() {
    std::string seed;
#ifdef _WIN32
    HKEY hk;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Cryptography", 0,
                      KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
        char val[256];
        DWORD sz = sizeof(val);
        DWORD type = 0;
        if (RegQueryValueExA(hk, "MachineGuid", nullptr, &type,
                             reinterpret_cast<LPBYTE>(val), &sz) == ERROR_SUCCESS &&
            type == REG_SZ) {
            seed.assign(val, sz > 0 ? sz - 1 : 0);  // strip NUL
        }
        RegCloseKey(hk);
    }
    if (seed.empty()) {
        char name[256];
        DWORD n = sizeof(name);
        if (GetComputerNameA(name, &n)) seed.assign(name, n);
    }
#else
    if (const char* h = std::getenv("HOSTNAME")) seed = h;
    if (seed.empty()) seed = "unknown-host";
#endif
    if (seed.empty()) seed = "unknown-machine";
    return sha256Hex("MGUID:" + seed).substr(0, 32);
}

// ===========================================================================
// Token verification (spec §5)
// ===========================================================================
TokenInfo verifyToken(const std::string& tokenB64, const std::string& sigB64,
                      const std::string& expectedDeviceId,
                      const std::string& expectedPrefix2) {
    TokenInfo info;
    if (tokenB64.empty() || sigB64.empty()) return info;
    std::vector<unsigned char> raw, sig;
    if (!base64Decode(tokenB64, raw)) return info;
    if (!base64Decode(sigB64, sig) || sig.size() != 64) return info;
    if (!ed25519Verify(kPublicKey, raw.data(), raw.size(), sig.data())) return info;

    std::string json(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::string dev = jsonGetString(json, "device_id");
    std::string prod = jsonGetString(json, "product");
    if (dev != expectedDeviceId) return info;
    if (!expectedPrefix2.empty() && prod.substr(0, 2) != expectedPrefix2) return info;

    info.ok = true;
    info.key = jsonGetString(json, "key");
    info.product = prod;
    info.edition = jsonGetString(json, "edition");
    info.deviceId = dev;
    return info;
}

// ===========================================================================
// Persistent state — Windows registry HKCU\Software\Blitzball\<appKey>
// ===========================================================================
namespace {
#ifdef _WIN32
std::string regPath(const std::string& appKey) {
    return "Software\\Blitzball\\" + appKey;
}
std::string regReadStr(const std::string& appKey, const char* name) {
    HKEY hk;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, regPath(appKey).c_str(), 0, KEY_READ, &hk) !=
        ERROR_SUCCESS)
        return "";
    std::string out;
    DWORD type = 0, sz = 0;
    if (RegQueryValueExA(hk, name, nullptr, &type, nullptr, &sz) == ERROR_SUCCESS &&
        type == REG_SZ && sz > 0) {
        std::vector<char> buf(sz);
        if (RegQueryValueExA(hk, name, nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf.data()), &sz) == ERROR_SUCCESS)
            out.assign(buf.data(), sz > 0 ? sz - 1 : 0);
    }
    RegCloseKey(hk);
    return out;
}
void regWriteStr(const std::string& appKey, const char* name, const std::string& v) {
    HKEY hk;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, regPath(appKey).c_str(), 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExA(hk, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(v.c_str()),
                   (DWORD)(v.size() + 1));
    RegCloseKey(hk);
}
DWORD regReadDword(const std::string& appKey, const char* name, DWORD def) {
    HKEY hk;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, regPath(appKey).c_str(), 0, KEY_READ, &hk) !=
        ERROR_SUCCESS)
        return def;
    DWORD v = def, sz = sizeof(v), type = 0;
    if (RegQueryValueExA(hk, name, nullptr, &type, reinterpret_cast<LPBYTE>(&v), &sz) !=
            ERROR_SUCCESS ||
        type != REG_DWORD)
        v = def;
    RegCloseKey(hk);
    return v;
}
void regWriteDword(const std::string& appKey, const char* name, DWORD v) {
    HKEY hk;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, regPath(appKey).c_str(), 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExA(hk, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(hk);
}
void regDelValue(const std::string& appKey, const char* name) {
    HKEY hk;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, regPath(appKey).c_str(), 0, KEY_WRITE, &hk) ==
        ERROR_SUCCESS) {
        RegDeleteValueA(hk, name);
        RegCloseKey(hk);
    }
}
#endif // _WIN32
} // namespace

StoredActivation loadActivation(const std::string& appKey) {
    StoredActivation s;
#ifdef _WIN32
    s.key = regReadStr(appKey, "key");
    s.tokenB64 = regReadStr(appKey, "token");
    s.sigB64 = regReadStr(appKey, "sig");
    s.edition = regReadStr(appKey, "edition");
    s.present = !s.tokenB64.empty() && !s.sigB64.empty();
#endif
    return s;
}

bool saveActivation(const std::string& appKey, const std::string& key,
                    const std::string& tokenB64, const std::string& sigB64,
                    const std::string& edition) {
#ifdef _WIN32
    regWriteStr(appKey, "key", key);
    regWriteStr(appKey, "token", tokenB64);
    regWriteStr(appKey, "sig", sigB64);
    regWriteStr(appKey, "edition", edition);
    return true;
#else
    (void)appKey; (void)key; (void)tokenB64; (void)sigB64; (void)edition;
    return false;
#endif
}

void clearActivation(const std::string& appKey) {
#ifdef _WIN32
    regDelValue(appKey, "key");
    regDelValue(appKey, "token");
    regDelValue(appKey, "sig");
    regDelValue(appKey, "edition");
#else
    (void)appKey;
#endif
}

int getTrialRuns(const std::string& appKey) {
#ifdef _WIN32
    return (int)regReadDword(appKey, "trialRuns", 0);
#else
    (void)appKey; return 0;
#endif
}
void setTrialRuns(const std::string& appKey, int runs) {
#ifdef _WIN32
    regWriteDword(appKey, "trialRuns", (DWORD)runs);
#else
    (void)appKey; (void)runs;
#endif
}

// ===========================================================================
// High-level evaluation (rule A: run1=30min, run2=10min, run>=3=locked)
// ===========================================================================
int allowanceForRun(int run) {
    if (run <= 1) return 30 * 60;
    if (run == 2) return 10 * 60;
    return 0;  // locked
}

Status evaluate(const std::string& appKey, const std::string& expectedPrefix2) {
    Status st;
    std::string dev = deviceId();
    StoredActivation act = loadActivation(appKey);
    if (act.present) {
        TokenInfo ti = verifyToken(act.tokenB64, act.sigB64, dev, expectedPrefix2);
        if (ti.ok) {
            st.state = State::Activated;
            st.edition = ti.edition;
            st.key = ti.key;
            return st;
        }
        // Token failed (tampered / different machine / wrong product): fall through
        // to trial. Do NOT auto-clear — a legitimate re-activation will overwrite.
    }
    int committed = getTrialRuns(appKey);
    int run = committed + 1;
    int allow = allowanceForRun(run);
    st.trialRun = run;
    st.allowanceSeconds = allow;
    st.state = allow > 0 ? State::Trial : State::Locked;
    return st;
}

void commitTrialRun(const std::string& appKey, int run) {
    if (getTrialRuns(appKey) < run) setTrialRuns(appKey, run);
}

} // namespace lic
