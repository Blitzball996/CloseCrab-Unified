// LicenseGate implementation — see LicenseGate.h.
#include "LicenseGate.h"

#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace closecrab {

namespace {
const char* envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

// --- Language resolution (English default, switchable) ----------------------
// Priority: CC_LANG / CC_LICENSE_LANG / LANG / LC_ALL  >  OS UI language  >  English.
// Set CC_LANG=zh (or en) to force a language.
bool resolveEnglish() {
    auto classify = [](const char* v) -> int {  // 1=en, 0=zh, -1=unknown
        if (!v || !*v) return -1;
        std::string s(v);
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        if (s.rfind("zh", 0) == 0 || s.find("chinese") != std::string::npos ||
            s.find("hans") != std::string::npos || s.find("hant") != std::string::npos)
            return 0;
        if (s.rfind("en", 0) == 0 || s.find("english") != std::string::npos) return 1;
        return -1;
    };
    for (const char* e : {"CC_LANG", "CC_LICENSE_LANG", "LANG", "LC_ALL"}) {
        int r = classify(std::getenv(e));
        if (r >= 0) return r == 1;
    }
#ifdef _WIN32
    if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE) return false;
#endif
    return true;  // default: English
}
bool isEnglish() {
    static bool cached = resolveEnglish();
    return cached;
}
// Pick the right string for the active language. Returns a const char* so it
// can be used directly as a printf format string AND concatenated with std::string.
const char* tr(const char* en, const char* zh) { return isEnglish() ? en : zh; }

size_t writeCb(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

// POST jsonBody to url, returning {httpCode, responseBody}. httpCode 0 on transport error.
struct HttpResp { long code = 0; std::string body; std::string curlErr; };
HttpResp httpPostJson(const std::string& url, const std::string& jsonBody,
                      const std::string& proxy) {
    HttpResp resp;
    CURL* curl = curl_easy_init();
    if (!curl) { resp.curlErr = "curl init failed"; return resp; }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "CloseCrab-Unified/0.2.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (!proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
    // else: libcurl auto-honours *_proxy env vars.

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.code);
    if (rc != CURLE_OK) resp.curlErr = curl_easy_strerror(rc);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

// Friendly localized text for a server/spec error code.
std::string errText(const std::string& code) {
    if (code == "NOT_FOUND")
        return tr("License key not found.", "查无此序列号");
    if (code == "ALREADY_ACTIVATED")
        return tr("This key is already activated on another device (one key, one device).",
                  "该序列号已在另一台设备激活（一码一机）");
    if (code == "REVOKED")
        return tr("This key has been revoked / refunded.", "该序列号已被吊销/退款");
    if (code == "WRONG_PRODUCT")
        return tr("Key does not match this product (CloseCrab only accepts CC keys).",
                  "序列号与本软件不符（CloseCrab 只接受 CC 开头）");
    if (code == "BAD_CHECKSUM")
        return tr("Invalid key (checksum failed — please check your input).",
                  "序列号校验失败（请检查输入）");
    if (code == "BAD_FORMAT")
        return tr("Invalid key format.", "序列号格式错误");
    if (code == "RATE_LIMITED")
        return tr("Too many activation attempts, please try again later.",
                  "激活过于频繁，请稍后再试");
    if (code == "NETWORK")
        return tr("Network error — could not reach the activation server "
                  "(if you use a proxy, check clash port 7897).",
                  "网络错误，无法连接激活服务器（如使用代理请检查 clash 端口 7897）");
    if (code == "BAD_TOKEN")
        return tr("Activation token signature verification failed.", "激活令牌签名校验失败");
    return tr("Activation failed (", "激活失败（") + code + tr(")", "）");
}

std::atomic<bool> g_trialThreadStarted{false};

// --- Saved proxy (registry HKCU\Software\Blitzball\CloseCrab\ProxyUrl) -------
#ifdef _WIN32
std::string regReadProxy() {
    HKEY h; std::string out;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Blitzball\\CloseCrab",
                      0, KEY_READ, &h) == ERROR_SUCCESS) {
        char buf[512]; DWORD sz = sizeof(buf) - 1, type = 0;
        if (RegQueryValueExA(h, "ProxyUrl", nullptr, &type, (LPBYTE)buf, &sz)
                == ERROR_SUCCESS && type == REG_SZ) {
            buf[(sz < sizeof(buf)) ? sz : sizeof(buf) - 1] = '\0';
            out = buf;
        }
        RegCloseKey(h);
    }
    return out;
}
void regWriteProxy(const std::string& v) {
    HKEY h;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Blitzball\\CloseCrab",
                        0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr)
            == ERROR_SUCCESS) {
        RegSetValueExA(h, "ProxyUrl", 0, REG_SZ,
                       (const BYTE*)v.c_str(), (DWORD)(v.size() + 1));
        RegCloseKey(h);
    }
}
#else
std::string regReadProxy() { return ""; }
void regWriteProxy(const std::string&) {}
#endif
} // namespace

std::string LicenseGate::baseUrl() {
    return envOr("CC_LICENSE_URL", lic::kDefaultBaseUrl());
}

std::string LicenseGate::proxy() {
    std::string saved = regReadProxy();
    if (!saved.empty()) return saved;
    if (const char* p = std::getenv("CC_LICENSE_PROXY")) { if (*p) return p; }
    if (const char* p = std::getenv("ALL_PROXY"))        { if (*p) return p; }
    if (const char* p = std::getenv("HTTPS_PROXY"))      { if (*p) return p; }
    if (const char* p = std::getenv("https_proxy"))      { if (*p) return p; }
    return "";  // libcurl will still pick up *_proxy env automatically
}

void LicenseGate::setProxy(const std::string& url) { regWriteProxy(url); }

lic::Status LicenseGate::status() {
    return lic::evaluate(kAppKey, kPrefix2);
}

ActivateResult LicenseGate::activate(const std::string& rawKey) {
    ActivateResult out;

    // 1. Local validation first (prefix + checksum) — no network needed.
    lic::ParseResult pr = lic::parseKey(rawKey, kPrefix2);
    if (!pr.ok) {
        out.errorCode = pr.errCode;
        out.message = errText(pr.errCode);
        return out;
    }

    // 2. Online activation.
    std::string dev = lic::deviceId();
    nlohmann::json req = {
        {"key", pr.canonical}, {"device_id", dev},
        {"product", pr.product}, {"app_version", kAppVersion}};
    std::string url = baseUrl() + "/api/license/activate";

    // Try, in order: user-saved/env proxy, direct, then common local proxy ports
    // (clash-verge 7897, clash 7890, v2rayN 10809). First one that yields an HTTP
    // response wins — so most users never need to configure anything.
    std::vector<std::string> cands;
    auto addCand = [&cands](const std::string& p) {
        for (const auto& c : cands) if (c == p) return;
        cands.push_back(p);
    };
    std::string userProxy = proxy();
    if (!userProxy.empty()) addCand(userProxy);
    addCand("");                       // direct connection
    addCand("http://127.0.0.1:7897");  // clash-verge default
    addCand("http://127.0.0.1:7890");  // clash classic default
    addCand("http://127.0.0.1:10809"); // v2rayN default

    HttpResp r;
    for (const auto& p : cands) {
        r = httpPostJson(url, req.dump(), p);
        if (r.code != 0) break;        // got a response (any HTTP status)
    }

    if (r.code == 0) {
        out.errorCode = "NETWORK";
        out.message = errText("NETWORK") + (r.curlErr.empty() ? "" : " [" + r.curlErr + "]");
        return out;
    }

    nlohmann::json j;
    try { j = nlohmann::json::parse(r.body); }
    catch (...) {
        out.errorCode = "NETWORK";
        out.message = tr("Unexpected server response (HTTP ", "服务器返回异常（HTTP ") +
                      std::to_string(r.code) + tr(").", "）");
        return out;
    }

    bool okFlag = j.value("ok", false);
    std::string errCode = j.value("error", okFlag ? std::string("OK") : std::string("UNKNOWN"));
    if (!okFlag) {
        out.errorCode = errCode;
        out.message = errText(errCode);
        return out;
    }

    std::string token = j.value("token", "");
    std::string sig = j.value("sig", "");
    std::string edition = j.value("edition", "");

    // 3. Verify the signed token offline before trusting/persisting it.
    lic::TokenInfo ti = lic::verifyToken(token, sig, dev, kPrefix2);
    if (!ti.ok) {
        out.errorCode = "BAD_TOKEN";
        out.message = errText("BAD_TOKEN");
        return out;
    }

    // 4. Persist.
    lic::saveActivation(kAppKey, ti.key, token, sig, edition.empty() ? ti.edition : edition);
    out.ok = true;
    out.errorCode = "OK";
    out.edition = edition.empty() ? ti.edition : edition;
    out.message = tr("Activated! Edition: ", "激活成功！版本：") + out.edition +
                  tr(" (", "（") + ti.key + tr(")", "）");
    return out;
}

void LicenseGate::deactivate() {
    lic::clearActivation(kAppKey);
}

std::string LicenseGate::deviceId() {
    return lic::deviceId();
}

ActivateResult LicenseGate::activateOffline(const std::string& blob) {
    ActivateResult out;
    // Expected format: "token|sig" or "token|sig|edition" (base64url token+sig).
    // Tolerate surrounding whitespace/newlines from copy-paste.
    std::string s = blob;
    auto trim = [](std::string& x) {
        size_t a = x.find_first_not_of(" \t\r\n");
        size_t b = x.find_last_not_of(" \t\r\n");
        x = (a == std::string::npos) ? "" : x.substr(a, b - a + 1);
    };
    trim(s);
    if (s.empty()) {
        out.errorCode = "BAD_TOKEN";
        out.message = tr("Empty offline activation code.", "离线激活码为空。");
        return out;
    }
    std::string token, sig, edition;
    size_t p1 = s.find('|');
    if (p1 == std::string::npos) {
        out.errorCode = "BAD_TOKEN";
        out.message = tr("Invalid offline code format (expected token|sig).",
                         "离线激活码格式错误（应为 token|sig）。");
        return out;
    }
    token = s.substr(0, p1);
    size_t p2 = s.find('|', p1 + 1);
    if (p2 == std::string::npos) {
        sig = s.substr(p1 + 1);
    } else {
        sig = s.substr(p1 + 1, p2 - p1 - 1);
        edition = s.substr(p2 + 1);
    }
    trim(token); trim(sig); trim(edition);

    // Verify the Ed25519 signature offline against this device — no network.
    std::string dev = lic::deviceId();
    lic::TokenInfo ti = lic::verifyToken(token, sig, dev, kPrefix2);
    if (!ti.ok) {
        out.errorCode = "BAD_TOKEN";
        out.message = errText("BAD_TOKEN") +
            tr(" (the code must be issued for THIS device's ID)",
               "（激活码必须是为本机设备指纹签发的）");
        return out;
    }

    lic::saveActivation(kAppKey, ti.key, token, sig, edition.empty() ? ti.edition : edition);
    out.ok = true;
    out.errorCode = "OK";
    out.edition = edition.empty() ? ti.edition : edition;
    out.message = tr("Activated offline! Edition: ", "离线激活成功！版本：") + out.edition +
                  tr(" (", "（") + ti.key + tr(")", "）");
    return out;
}

void LicenseGate::printStatus() {
    lic::Status s = status();
    switch (s.state) {
        case lic::State::Activated:
            std::printf(tr("[CloseCrab License] Activated — edition %s (%s)\n",
                           "[CloseCrab 授权] 已激活 — 版本 %s (%s)\n"),
                        s.edition.c_str(), s.key.c_str());
            break;
        case lic::State::Trial:
            std::printf(tr("[CloseCrab License] Trial — run #%d, %d minutes this session\n",
                           "[CloseCrab 授权] 试用模式 — 第 %d 次，本次额度 %d 分钟\n"),
                        s.trialRun, s.allowanceSeconds / 60);
            break;
        case lic::State::Locked:
            std::printf(tr("[CloseCrab License] Trial used up — activate: closecrab --activate <KEY>\n",
                           "[CloseCrab 授权] 试用已用尽，请激活：closecrab --activate <序列号>\n"));
            break;
    }
    std::printf(tr("        Device ID: %s\n        Server: %s\n",
                   "        设备指纹: %s\n        激活服务器: %s\n"),
                lic::deviceId().c_str(), baseUrl().c_str());
}

bool LicenseGate::enforceAtStartup() {
    lic::Status s = status();

    if (s.state == lic::State::Activated) {
        spdlog::info("License: activated ({} {})", s.edition, s.key);
        std::printf(tr("\033[32m✓ CloseCrab activated (%s)\033[0m\n",
                       "\033[32m✓ CloseCrab 已激活（%s）\033[0m\n"),
                    s.edition.c_str());
        return true;
    }

    // Helper: read one line of serial input from the user at startup. Runs on the
    // main thread before the REPL/key-watcher start, so plain std::getline is safe.
    auto promptForSerial = [](const std::string& hint) -> std::string {
        std::printf("%s", hint.c_str());
        std::fflush(stdout);
        std::string line;
        if (!std::getline(std::cin, line)) return "";  // EOF
        size_t a = line.find_first_not_of(" \t\r\n");
        size_t b = line.find_last_not_of(" \t\r\n");
        return (a == std::string::npos) ? "" : line.substr(a, b - a + 1);
    };

    // Activate, and if it fails purely on the network, offer to set a proxy and retry.
    auto activateWithProxyRetry = [&](const std::string& key) -> ActivateResult {
        std::printf(tr("Activating online…\n", "正在联网激活…\n"));
        auto res = activate(key);
        if (!res.ok && res.errorCode == "NETWORK") {
            std::string p = promptForSerial(tr(
                "\nCould not reach the server. If you use a proxy (clash/v2ray), "
                "enter it to retry, e.g. http://127.0.0.1:7897 (Enter to skip): ",
                "\n无法连接服务器。若使用代理(clash/v2ray)，请输入代理地址重试，"
                "例如 http://127.0.0.1:7897（直接回车跳过）: "));
            if (!p.empty()) {
                setProxy(p);
                std::printf(tr("Retrying via %s …\n", "正在通过 %s 重试…\n"), p.c_str());
                res = activate(key);
            }
        }
        // Still failing on the network → offer OFFLINE activation. The user gets a
        // signed token for THIS device's ID from the website / another device and
        // pastes it back; we verify it offline (no network needed).
        if (!res.ok && res.errorCode == "NETWORK") {
            std::printf(tr(
                "\n\033[36m--- Offline activation ---\033[0m\n"
                " Still offline. You can activate WITHOUT internet:\n"
                "  1) On a device that has internet, open: %s/activate\n"
                "  2) Enter your license key AND this Device ID:\n"
                "     \033[1m%s\033[0m\n"
                "  3) Copy the offline code it gives you (format: token|sig|edition)\n",
                "\n\033[36m--- 离线激活 ---\033[0m\n"
                " 仍然无法联网。你可以在没有网络的情况下激活：\n"
                "  1) 在一台有网络的设备上打开：%s/activate\n"
                "  2) 输入你的序列号和本机设备指纹：\n"
                "     \033[1m%s\033[0m\n"
                "  3) 复制它给出的离线激活码（格式：token|sig|edition）\n"),
                baseUrl().c_str(), deviceId().c_str());
            std::string blob = promptForSerial(tr(
                " Paste the offline code here (Enter to skip): ",
                " 在此粘贴离线激活码（直接回车跳过）: "));
            if (!blob.empty()) {
                res = activateOffline(blob);
            }
        }
        return res;
    };

    if (s.state == lic::State::Locked) {
        std::printf(tr("\n\033[31m============== CloseCrab — Not Activated ==============\033[0m\n",
                       "\n\033[31m================ CloseCrab 未激活 ================\033[0m\n"));
        std::printf(tr(" Trial used up (first run 30 min + second run 10 min).\n",
                       " 试用已用尽（首次 30 分钟 + 第二次 10 分钟）。\n"));
        std::printf(tr(" You must enter a license key to continue.\n",
                       " 必须输入序列号激活才能继续使用。\n"));
        std::printf(tr(" Key format: \033[36mCCST/CCPR-XXXX-XXXX-XXXX-XXXX\033[0m\n",
                       " 序列号格式：\033[36mCCST/CCPR-XXXX-XXXX-XXXX-XXXX\033[0m\n"));
        std::printf(tr(" Look up your key by email at https://blitzball.lol\n",
                       " 购买后凭邮箱在 https://blitzball.lol 查询序列号。\n"));
        std::printf(tr("\033[31m======================================================\033[0m\n",
                       "\033[31m=================================================\033[0m\n"));
        for (;;) {
            std::string key = promptForSerial(
                tr("\nEnter license key (q to quit): ", "\n请输入序列号激活（输入 q 退出）: "));
            if (key.empty() || key == "q" || key == "Q" || key == "quit") {
                std::printf(tr("Exited.\n", "已退出。\n"));
                return false;
            }
            auto res = activateWithProxyRetry(key);
            std::printf("%s\n", res.message.c_str());
            if (res.ok) return true;
        }
    }

    // Trial: announce, offer activation now, otherwise start the countdown.
    const int run = s.trialRun;
    const int allowance = s.allowanceSeconds;
    std::printf(tr("\n\033[33m================ CloseCrab — Trial ================\033[0m\n",
                   "\n\033[33m================ CloseCrab 试用模式 ================\033[0m\n"));
    std::printf(tr(" Not activated — trial run #%d, \033[1m%d minutes\033[0m available this session.\n",
                   " 当前未激活 —— 第 %d 次试用，本次可用 \033[1m%d 分钟\033[0m。\n"),
                run, allowance / 60);
    std::printf(tr(" Rule: 30 min first run, 10 min second run, then activation required.\n",
                   " 规则：首次 30 分钟，第二次 10 分钟，之后需激活。\n"));
    std::printf(tr(" Key format: \033[36mCCST/CCPR-XXXX-XXXX-XXXX-XXXX\033[0m\n",
                   " 序列号格式：\033[36mCCST/CCPR-XXXX-XXXX-XXXX-XXXX\033[0m\n"));
    std::printf(tr("\033[33m===================================================\033[0m\n",
                   "\033[33m===================================================\033[0m\n"));

    std::string key = promptForSerial(
        tr("\nEnter a key to activate now, or press Enter to start the trial: ",
           "\n现在输入序列号激活，或直接按回车开始试用: "));
    if (!key.empty()) {
        auto res = activateWithProxyRetry(key);
        std::printf("%s\n", res.message.c_str());
        if (res.ok) return true;  // activated — no trial limit
        std::printf(tr("\033[33mContinuing in trial mode (%d minutes this session).\033[0m\n",
                       "\033[33m将以试用模式继续（本次 %d 分钟）。\033[0m\n"),
                    allowance / 60);
    } else {
        std::printf(tr("\033[33mTrial started: %d minutes this session, the app will exit when it ends.\033[0m\n",
                       "\033[33m已开始试用：本次 %d 分钟，到时将自动退出。\033[0m\n"),
                    allowance / 60);
    }

    if (!g_trialThreadStarted.exchange(true)) {
        std::thread([run, allowance]() {
            // Count the run as "used" only after 60s of real use (spec: >60s).
            int firstSleep = allowance < 60 ? allowance : 60;  // avoid std::min vs windows.h min macro
            std::this_thread::sleep_for(std::chrono::seconds(firstSleep));
            lic::commitTrialRun(LicenseGate::kAppKey, run);
            int remain = allowance - 60;
            if (remain > 0)
                std::this_thread::sleep_for(std::chrono::seconds(remain));
            std::printf(tr("\n\033[31m[CloseCrab] Trial time (%d min) is over, exiting. "
                           "Activate to continue: closecrab --activate <KEY>\033[0m\n",
                           "\n\033[31m[CloseCrab] 试用时间（%d 分钟）已结束，程序退出。"
                           "请激活后继续使用：closecrab --activate <序列号>\033[0m\n"),
                        allowance / 60);
            std::fflush(stdout);
            std::_Exit(0);  // libc++ (macOS) lacks std::quick_exit; _Exit is portable
        }).detach();
    }
    return true;
}

} // namespace closecrab
