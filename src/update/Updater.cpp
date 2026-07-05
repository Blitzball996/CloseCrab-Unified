#include "Updater.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <sys/utsname.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static size_t writeString(char* ptr, size_t sz, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, sz * nmemb);
    return sz * nmemb;
}

static size_t writeFile(char* ptr, size_t sz, size_t nmemb, void* userdata) {
    static_cast<std::ofstream*>(userdata)->write(ptr, static_cast<std::streamsize>(sz * nmemb));
    return sz * nmemb;
}

static std::string curlGet(const std::string& url, int timeoutSec = 8) {
    std::string body;
    CURL* c = curl_easy_init();
    if (!c) return body;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "User-Agent: closecrab-updater/1.0");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)timeoutSec);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_perform(c);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    return body;
}

static bool curlDownload(const std::string& url, const fs::path& dest, int timeoutSec = 120) {
    std::ofstream f(dest, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    CURL* c = curl_easy_init();
    if (!c) return false;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "User-Agent: closecrab-updater/1.0");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &f);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)timeoutSec);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    CURLcode res = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    f.close();
    return res == CURLE_OK && code == 200;
}

// Compare two "MAJOR.MINOR.PATCH" version strings. Returns true if b > a.
static bool isNewer(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& v) {
        // Strip leading 'v'
        std::string s = v;
        if (!s.empty() && s[0] == 'v') s = s.substr(1);
        std::vector<int> parts(3, 0);
        std::istringstream ss(s);
        std::string tok;
        int i = 0;
        while (std::getline(ss, tok, '.') && i < 3)
            parts[i++] = std::stoi(tok);
        return parts;
    };
    auto va = parse(a), vb = parse(b);
    return vb > va;
}

static fs::path exePath() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf);
#elif defined(__APPLE__)
    char buf[4096] = {};
    uint32_t sz = sizeof(buf);
    _NSGetExecutablePath(buf, &sz);
    return fs::canonical(buf);
#else
    return fs::canonical("/proc/self/exe");
#endif
}

// Pick the release asset name for this platform/arch.
static std::string platformAsset() {
#ifdef _WIN32
    return "CloseCrab-Windows-x64.zip";
#elif defined(__APPLE__)
    return "CloseCrab-macOS-universal.tar.gz";
#else
    struct utsname u{};
    uname(&u);
    std::string m = u.machine;
    if (m == "aarch64" || m == "arm64")
        return "CloseCrab-Linux-arm64.tar.gz";
    return "CloseCrab-Linux-x86_64.tar.gz";
#endif
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

closecrab::UpdateInfo closecrab::checkForUpdate() {
    UpdateInfo info;
#ifndef CLOSECRAB_VERSION
    info.errorMsg = "version not baked into binary";
    return info;
#else
    info.currentVersion = CLOSECRAB_VERSION;
#endif

    const std::string apiUrl =
        "https://api.github.com/repos/Blitzball996/CloseCrab-Unified/releases/latest";

    std::string body = curlGet(apiUrl);
    if (body.empty()) {
        info.errorMsg = "network error — could not reach GitHub API";
        return info;
    }

    try {
        auto j = json::parse(body);
        std::string tag = j.value("tag_name", "");
        if (tag.empty()) {
            info.errorMsg = "no tag_name in GitHub response";
            return info;
        }
        info.latestVersion = (tag[0] == 'v') ? tag.substr(1) : tag;

        // Find the asset for this platform
        info.assetName = platformAsset();
        if (j.contains("assets") && j["assets"].is_array()) {
            for (const auto& asset : j["assets"]) {
                if (asset.value("name", "") == info.assetName) {
                    info.downloadUrl = asset.value("browser_download_url", "");
                    break;
                }
            }
        }

        if (info.downloadUrl.empty()) {
            info.errorMsg = "asset '" + info.assetName + "' not found in latest release";
            return info;
        }

        info.hasUpdate = isNewer(info.currentVersion, info.latestVersion);
    } catch (const std::exception& e) {
        info.errorMsg = std::string("parse error: ") + e.what();
    }
    return info;
}

bool closecrab::installUpdate(UpdateInfo& info) {
    fs::path exeFile = exePath();
    fs::path tmpDir  = fs::temp_directory_path() / ("closecrab-update-" +
                       std::to_string(std::chrono::system_clock::now()
                           .time_since_epoch().count()));
    fs::create_directories(tmpDir);

    // Download archive
    fs::path archivePath = tmpDir / info.assetName;
    spdlog::info("Updater: downloading {} ...", info.downloadUrl);
    if (!curlDownload(info.downloadUrl, archivePath)) {
        info.errorMsg = "download failed";
        fs::remove_all(tmpDir);
        return false;
    }

    // Extract binary
    fs::path newBin;
#ifdef _WIN32
    // Use PowerShell to extract the zip
    std::string cmd = "powershell -NoProfile -Command \"Expand-Archive -Path '" +
                      archivePath.string() + "' -DestinationPath '" +
                      tmpDir.string() + "' -Force\"";
    if (std::system(cmd.c_str()) != 0) {
        info.errorMsg = "extraction failed";
        fs::remove_all(tmpDir);
        return false;
    }
    newBin = tmpDir / "CloseCrab" / "closecrab.exe";
#else
    std::string cmd = "tar -xzf '" + archivePath.string() +
                      "' -C '" + tmpDir.string() + "' CloseCrab/closecrab 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) {
        info.errorMsg = "extraction failed";
        fs::remove_all(tmpDir);
        return false;
    }
    newBin = tmpDir / "CloseCrab" / "closecrab";
#endif

    if (!fs::exists(newBin)) {
        info.errorMsg = "extracted binary not found";
        fs::remove_all(tmpDir);
        return false;
    }

#ifdef _WIN32
    // On Windows we cannot replace a running exe in-place.
    // Drop a .bat helper that does the swap after the process exits.
    fs::path newExe  = exeFile.parent_path() / "closecrab.new.exe";
    fs::path batPath = exeFile.parent_path() / "closecrab-update.bat";
    fs::copy_file(newBin, newExe, fs::copy_options::overwrite_existing);

    std::ofstream bat(batPath);
    bat << "@echo off\n"
        << "ping -n 3 127.0.0.1 > nul\n"          // wait ~2s for parent to exit
        << "move /y \"" << newExe.string() << "\" \""
                        << exeFile.string() << "\"\n"
        << "del \"%~f0\"\n";                        // delete this bat
    bat.close();

    // Launch the bat detached
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring batW = batPath.wstring();
    std::wstring cmd2 = L"cmd.exe /c \"" + batW + L"\"";
    if (!CreateProcessW(nullptr, cmd2.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | DETACHED_PROCESS,
                        nullptr, nullptr, &si, &pi)) {
        info.errorMsg = "could not launch update helper";
        fs::remove_all(tmpDir);
        return false;
    }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
#else
    // Unix: replace the binary atomically. The old inode stays mapped until
    // the current process exits — safe to rename over a running executable.
    fs::permissions(newBin, fs::perms::owner_exec | fs::perms::group_exec |
                             fs::perms::others_exec | fs::perms::owner_read |
                             fs::perms::group_read  | fs::perms::others_read |
                             fs::perms::owner_write,
                    fs::perm_options::add);
    std::error_code ec;
    fs::rename(newBin, exeFile, ec);
    if (ec) {
        // rename across filesystems isn't atomic — copy+delete as fallback
        fs::copy_file(newBin, exeFile, fs::copy_options::overwrite_existing);
    }
#endif

    fs::remove_all(tmpDir);
    return true;
}
