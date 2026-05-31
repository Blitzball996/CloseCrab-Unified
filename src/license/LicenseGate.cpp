// LicenseGate implementation — see LicenseGate.h.
#include "LicenseGate.h"

#include <cstdlib>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace closecrab {

namespace {
const char* envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

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

// Friendly Chinese text for a server/spec error code.
std::string errText(const std::string& code) {
    if (code == "NOT_FOUND")          return "查无此序列号";
    if (code == "ALREADY_ACTIVATED")  return "该序列号已在另一台设备激活（一码一机）";
    if (code == "REVOKED")            return "该序列号已被吊销/退款";
    if (code == "WRONG_PRODUCT")      return "序列号与本软件不符（CloseCrab 只接受 CC 开头）";
    if (code == "BAD_CHECKSUM")       return "序列号校验失败（请检查输入）";
    if (code == "BAD_FORMAT")         return "序列号格式错误";
    if (code == "RATE_LIMITED")       return "激活过于频繁，请稍后再试";
    if (code == "NETWORK")            return "网络错误，无法连接激活服务器（如使用代理请检查 clash 端口 7897）";
    if (code == "BAD_TOKEN")          return "激活令牌签名校验失败";
    return "激活失败（" + code + "）";
}

std::atomic<bool> g_trialThreadStarted{false};
} // namespace

std::string LicenseGate::baseUrl() {
    return envOr("CC_LICENSE_URL", lic::kDefaultBaseUrl());
}

std::string LicenseGate::proxy() {
    if (const char* p = std::getenv("CC_LICENSE_PROXY")) { if (*p) return p; }
    if (const char* p = std::getenv("ALL_PROXY"))        { if (*p) return p; }
    if (const char* p = std::getenv("HTTPS_PROXY"))      { if (*p) return p; }
    if (const char* p = std::getenv("https_proxy"))      { if (*p) return p; }
    return "";  // libcurl will still pick up *_proxy env automatically
}

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
    HttpResp r = httpPostJson(url, req.dump(), proxy());

    if (r.code == 0) {
        out.errorCode = "NETWORK";
        out.message = errText("NETWORK") + (r.curlErr.empty() ? "" : " [" + r.curlErr + "]");
        return out;
    }

    nlohmann::json j;
    try { j = nlohmann::json::parse(r.body); }
    catch (...) {
        out.errorCode = "NETWORK";
        out.message = "服务器返回异常（HTTP " + std::to_string(r.code) + "）";
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
    out.message = "激活成功！版本：" + out.edition + "（" + ti.key + "）";
    return out;
}

void LicenseGate::deactivate() {
    lic::clearActivation(kAppKey);
}

void LicenseGate::printStatus() {
    lic::Status s = status();
    switch (s.state) {
        case lic::State::Activated:
            std::printf("[CloseCrab 授权] 已激活 — 版本 %s (%s)\n",
                        s.edition.c_str(), s.key.c_str());
            break;
        case lic::State::Trial:
            std::printf("[CloseCrab 授权] 试用模式 — 第 %d 次，本次额度 %d 分钟\n",
                        s.trialRun, s.allowanceSeconds / 60);
            break;
        case lic::State::Locked:
            std::printf("[CloseCrab 授权] 试用已用尽，请激活：closecrab --activate <序列号>\n");
            break;
    }
    std::printf("        设备指纹: %s\n        激活服务器: %s\n",
                lic::deviceId().c_str(), baseUrl().c_str());
}

bool LicenseGate::enforceAtStartup() {
    lic::Status s = status();

    if (s.state == lic::State::Activated) {
        spdlog::info("License: activated ({} {})", s.edition, s.key);
        return true;
    }

    if (s.state == lic::State::Locked) {
        std::printf("\n\033[31m============================================\n");
        std::printf(" CloseCrab 试用已用尽（30 分钟 + 10 分钟）。\n");
        std::printf(" 请购买并激活：closecrab --activate <序列号>\n");
        std::printf(" 序列号格式：CCST/CCPR-XXXX-XXXX-XXXX-XXXX\n");
        std::printf("============================================\033[0m\n\n");
        return false;
    }

    // Trial: announce, start countdown, allow this session.
    const int run = s.trialRun;
    const int allowance = s.allowanceSeconds;
    std::printf("\n\033[33m============================================\n");
    std::printf(" CloseCrab 试用模式（第 %d 次）：本次可用 %d 分钟。\n",
                run, allowance / 60);
    std::printf(" 激活以解除限制：closecrab --activate <序列号>\n");
    std::printf("============================================\033[0m\n\n");

    if (!g_trialThreadStarted.exchange(true)) {
        std::thread([run, allowance]() {
            // Count the run as "used" only after 60s of real use (spec: >60s).
            std::this_thread::sleep_for(std::chrono::seconds(std::min(60, allowance)));
            lic::commitTrialRun(LicenseGate::kAppKey, run);
            int remain = allowance - 60;
            if (remain > 0)
                std::this_thread::sleep_for(std::chrono::seconds(remain));
            std::printf("\n\033[31m[CloseCrab] 试用时间（%d 分钟）已结束，程序退出。"
                        "请激活后继续使用：closecrab --activate <序列号>\033[0m\n",
                        allowance / 60);
            std::fflush(stdout);
            std::quick_exit(0);
        }).detach();
    }
    return true;
}

} // namespace closecrab
