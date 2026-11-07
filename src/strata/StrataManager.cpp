// StrataManager.cpp — lifecycle and hardware-probe bridge for Strata.
// See src/strata/StrataManager.h for the full API.

#include "StrataManager.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <winsock2.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <csignal>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace closecrab {

// ---- singleton ---------------------------------------------------------------

StrataManager& StrataManager::getInstance() {
    static StrataManager instance;
    return instance;
}

StrataManager::~StrataManager() {
    stop();
}

// ---- helpers -----------------------------------------------------------------

std::string StrataManager::defaultStrataRoot() {
    const char* env = std::getenv("STRATA_ROOT");
    if (env && *env) return env;

    // Walk up from the exe to find external/strata
    try {
        fs::path exe = fs::current_path();  // close enough for most cases
        for (auto candidate : {
                exe / "external" / "strata",
                exe.parent_path() / "external" / "strata",
                exe.parent_path().parent_path() / "external" / "strata"}) {
            if (fs::exists(candidate / "setup.py"))
                return candidate.string();
        }
    } catch (...) {}
    return "";
}

std::string StrataManager::findPython() {
    // 1. explicit override
    const char* env = std::getenv("STRATA_PYTHON");
    if (env && *env) return env;

    // 2. virtual environment beside the Strata root
    std::string root = defaultStrataRoot();
    if (!root.empty()) {
        for (auto rel : {".venv/bin/python3", ".venv/bin/python",
                         ".venv/Scripts/python.exe"}) {
            fs::path p = fs::path(root) / rel;
            if (fs::exists(p)) return p.string();
        }
    }

    // 3. system python3 / python
#if defined(_WIN32)
    for (const char* name : {"python3.exe", "python.exe"}) {
        // SearchPath is the Windows equivalent of `which`
        std::array<char, MAX_PATH> buf{};
        if (SearchPathA(nullptr, name, nullptr,
                        static_cast<DWORD>(buf.size()), buf.data(), nullptr))
            return buf.data();
    }
#else
    for (const char* name : {"python3", "python"}) {
        // Use `which`-style PATH walk
        const char* path = std::getenv("PATH");
        if (!path) continue;
        std::istringstream ss(path);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            fs::path p = fs::path(dir) / name;
            if (fs::exists(p)) return p.string();
        }
    }
#endif
    return "python";  // last resort
}

// ---- free port ---------------------------------------------------------------

static int pickFreePort() {
#if defined(_WIN32)
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    int fd = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) return 8080;
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        socklen_t len = sizeof(addr);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
            int p = ntohs(addr.sin_port);
#if defined(_WIN32)
            closesocket(fd);
#else
            close(fd);
#endif
            return p;
        }
    }
#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
    return 8080;
}

// ---- run probe script --------------------------------------------------------

static std::string runCapture(const std::string& cmd) {
    // popen-based capture (stdout only).
#if defined(_WIN32)
    FILE* fp = _popen(cmd.c_str(), "r");
#else
    FILE* fp = popen(cmd.c_str(), "r");
#endif
    if (!fp) return "";
    std::string out;
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), fp))
        out += buf.data();
#if defined(_WIN32)
    _pclose(fp);
#else
    pclose(fp);
#endif
    return out;
}

static StrataModel parseModelRow(const json& m) {
    StrataModel r;
    r.family       = m.value("family",       "");
    r.familyTitle  = m.value("family_title", "");
    r.model        = m.value("model",        "");
    r.id           = m.value("id",           "");
    r.about        = m.value("about",        "");
    r.downloadGb   = m.value("download_gb",  0.0);
    r.needVramGb   = m.value("need_vram_gb", 0.0);
    r.needRamGb    = m.value("need_ram_gb",  0.0);
    r.spillGb      = m.value("spill_gb",     0.0);
    r.verdict      = m.value("verdict",      "");
    r.words        = m.value("words",        "");
    r.recommended  = m.value("recommended",  false);
    r.installed    = m.value("installed",    false);
    r.experimental = m.value("experimental", false);
    return r;
}

StrataProbeResult StrataManager::probe(const std::string& strataRootIn) {
    StrataProbeResult result;

    std::string strataRoot = strataRootIn.empty() ? defaultStrataRoot() : strataRootIn;
    std::string python     = findPython();

    // Locate probe script beside this binary or in scripts/
    fs::path probe;
    for (auto p : {
            fs::path(strataRoot).parent_path().parent_path() / "scripts" / "strata_probe.py",
            fs::path(strataRoot).parent_path() / "scripts" / "strata_probe.py",
            fs::current_path() / "scripts" / "strata_probe.py"}) {
        if (fs::exists(p)) { probe = p; break; }
    }
    if (probe.empty()) {
        result.error = "strata_probe.py not found. Expected at scripts/strata_probe.py beside the project root.";
        return result;
    }

    // Build command: python scripts/strata_probe.py --strata-root <root>
    std::string cmd = "\"" + python + "\" \"" + probe.string() + "\"";
    if (!strataRoot.empty())
        cmd += " --strata-root \"" + strataRoot + "\"";

    spdlog::debug("StrataManager: probe command: {}", cmd);
    std::string raw = runCapture(cmd);

    if (raw.empty()) {
        result.error = "strata_probe.py produced no output. Is Python available? ("
                       + python + ")";
        return result;
    }

    json j;
    try { j = json::parse(raw); }
    catch (const std::exception& e) {
        result.error = std::string("strata_probe.py output is not valid JSON: ") + e.what()
                       + "\nRaw: " + raw.substr(0, 200);
        return result;
    }

    if (!j.value("ok", false)) {
        result.error = j.value("error", "unknown error from strata_probe.py");
        return result;
    }

    // hardware
    auto hw = j.value("hardware", json::object());
    result.hardware.gpuName    = hw.value("gpu_name",     "");
    result.hardware.vramGb     = hw.value("vram_gb",      0.0);
    result.hardware.gpuCount   = hw.value("gpu_count",    0);
    result.hardware.ramGb      = hw.value("ram_gb",       0.0);
    result.hardware.diskFreeGb = hw.value("disk_free_gb", 0.0);
    result.hardware.summary    = hw.value("summary",      "");
    result.hardware.backend    = hw.value("backend",      "");
    result.hardware.os         = hw.value("os",           "");

    // models
    for (auto& m : j.value("models", json::array()))
        result.models.push_back(parseModelRow(m));

    // recommended
    auto rec = j.value("recommended", json::object());
    if (!rec.is_null() && rec.contains("id"))
        result.recommended = parseModelRow(rec);

    result.ok = true;
    spdlog::info("StrataManager: probe ok — {} GPU {:.0f} GB, {} models found",
                 result.hardware.gpuName, result.hardware.vramGb, result.models.size());
    return result;
}

// ---- start -------------------------------------------------------------------

bool StrataManager::start(const std::string& modelId,
                          const std::string& strataRootIn,
                          int  timeoutSec,
                          int  portHint) {
    if (running_) {
        spdlog::warn("StrataManager: already running on port {}", port_);
        return true;
    }

    std::string strataRoot = strataRootIn.empty() ? defaultStrataRoot() : strataRootIn;
    std::string python     = findPython();
    port_                  = portHint > 0 ? portHint : pickFreePort();

    // python -m serve.server --engine strata --config <id>.json --port <port>
    // --no-start is NOT passed: we want it to actually start.
    std::string cmdline = "\"" + python + "\""
        + " -m serve.server"
        + " --engine strata"
        + " --config \"" + modelId + "\""
        + " --port " + std::to_string(port_)
        + " --quiet";   // suppress human-readable progress to stderr

    spdlog::info("StrataManager: starting server: {}", cmdline);

#if defined(_WIN32)
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput  = GetStdHandle(STD_ERROR_HANDLE);  // server logs → our stderr
    si.hStdError   = GetStdHandle(STD_ERROR_HANDLE);

    // CreateProcess needs a mutable buffer
    std::string mutable_cmd = cmdline;
    // Set working directory to Strata root so `python -m serve.server` resolves
    BOOL ok = CreateProcessA(
        nullptr,
        mutable_cmd.data(),
        nullptr, nullptr,
        /*bInheritHandles=*/TRUE,
        /*dwCreationFlags=*/0,
        nullptr,
        strataRoot.empty() ? nullptr : strataRoot.c_str(),
        &si, &pi);
    if (!ok) {
        spdlog::error("StrataManager: CreateProcess failed ({})", GetLastError());
        return false;
    }
    processHandle_ = pi.hProcess;
    processThread_ = pi.hThread;
#else
    pid_t pid = fork();
    if (pid < 0) {
        spdlog::error("StrataManager: fork failed");
        return false;
    }
    if (pid == 0) {
        // child
        if (!strataRoot.empty()) chdir(strataRoot.c_str());
        execl("/bin/sh", "sh", "-c", cmdline.c_str(), nullptr);
        _exit(127);
    }
    childPid_ = pid;
#endif

    running_ = true;
    if (!pollHealth(timeoutSec)) {
        spdlog::error("StrataManager: server did not become healthy within {} s", timeoutSec);
        stop();
        return false;
    }
    spdlog::info("StrataManager: Strata ready at {}", baseUrl());
    return true;
}

// ---- health poll -------------------------------------------------------------

bool StrataManager::pollHealth(int timeoutSec) const {
    // Strata exposes GET /health → 200 when ready.
    std::string url  = "http://127.0.0.1:" + std::to_string(port_) + "/health";
    std::string curl = "curl -sf --max-time 2 \"" + url + "\" > " +
#if defined(_WIN32)
        "NUL";
#else
        "/dev/null";
#endif

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::system(curl.c_str()) == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return false;
}

// ---- stop --------------------------------------------------------------------

void StrataManager::stop() {
    if (!running_) return;
    spdlog::info("StrataManager: stopping Strata server");
#if defined(_WIN32)
    if (processHandle_) {
        TerminateProcess(static_cast<HANDLE>(processHandle_), 0);
        WaitForSingleObject(static_cast<HANDLE>(processHandle_), 3000);
        CloseHandle(static_cast<HANDLE>(processHandle_));
        CloseHandle(static_cast<HANDLE>(processThread_));
        processHandle_ = nullptr;
        processThread_ = nullptr;
    }
#else
    if (childPid_ > 0) {
        kill(childPid_, SIGTERM);
        int status = 0;
        waitpid(childPid_, &status, 0);
        childPid_ = 0;
    }
#endif
    running_ = false;
    port_    = 0;
}

// ---- status ------------------------------------------------------------------

bool StrataManager::isRunning() const { return running_; }

std::string StrataManager::baseUrl() const {
    if (!running_) return "";
    return "http://127.0.0.1:" + std::to_string(port_);
}

} // namespace closecrab
