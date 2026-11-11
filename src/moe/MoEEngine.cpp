// MoEEngine.cpp — manages closecrab-moe-engine binary as a subprocess.
// Token protocol: write prompt ids (newline-separated int32) ending with a
// sentinel, read generated ids line-by-line from stdout until EOS marker.

#include "MoEEngine.h"
#include <spdlog/spdlog.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <io.h>
#  define READ_FD  _read
#  define WRITE_FD _write
#  define CLOSE_FD _close
#else
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  define READ_FD  read
#  define WRITE_FD write
#  define CLOSE_FD close
#endif

namespace fs = std::filesystem;
namespace closecrab::moe {

// ---- find engine binary ----------------------------------------------------

std::string MoEEngine::findEngineBinary() {
    // 1. CC_MOE_ENGINE env
    if (const char* env = std::getenv("CC_MOE_ENGINE"))
        if (fs::exists(env)) return env;
    // 2. beside closecrab.exe
    try {
        // The engine is built as closecrab-moe-engine(.exe)
        fs::path exeDir = fs::current_path();
        for (auto p : {
                exeDir / "closecrab-moe-engine.exe",
                exeDir / "closecrab-moe-engine",
                exeDir.parent_path() / "closecrab-moe-engine.exe",
                exeDir.parent_path() / "closecrab-moe-engine"}) {
            if (fs::exists(p)) return p.string();
        }
    } catch (...) {}
    // 3. fallback name
    return "closecrab-moe-engine";
}

std::string MoEEngine::engineVersion(const std::string& engineBin) {
    std::string cmd = "\"" + engineBin + "\" --version 2>nul";
#if defined(_WIN32)
    FILE* fp = _popen(cmd.c_str(), "r");
#else
    FILE* fp = popen(cmd.c_str(), "r");
#endif
    if (!fp) return "";
    std::array<char, 256> buf{};
    std::string out;
    if (fgets(buf.data(), static_cast<int>(buf.size()), fp)) out = buf.data();
#if defined(_WIN32)
    _pclose(fp);
#else
    pclose(fp);
#endif
    // strip trailing newline
    auto nl = out.find('\n');
    if (nl != std::string::npos) out.resize(nl);
    return out;
}

// ---- start -----------------------------------------------------------------

MoEEngine::MoEEngine() = default;
MoEEngine::~MoEEngine() { stop(); }

bool MoEEngine::start(const EngineConfig& cfg) {
    if (running_) { spdlog::warn("MoE: engine already running"); return true; }

    std::string bin = findEngineBinary();
    if (!fs::exists(bin)) {
        spdlog::error("MoE: engine binary not found at '{}'. "
                      "Build with -DUSE_MOE_ENGINE=ON first.", bin);
        return false;
    }

    // Engine serve-mode command line (confirmed from Strata bench/results):
    //   closecrab-moe-engine --pack <dir> --native <shard1.gguf> --ple-gguf <shard2.gguf> --serve ...
    // --pack <dir>       : directory containing native_experts.txt and pack metadata
    // --native <gguf>    : shard 1 (experts, largest file, -00001-of-00002)
    // --ple-gguf <gguf>  : shard 2 (PLE n-gram table, -00002-of-00002)
    // For single-file packs (no native_experts.txt), only --native is needed.

    // Scan cfg.modelPath for gguf files, sort by name so 00001 < 00002
    std::string shard1, shard2;
    std::error_code ec;
    std::vector<std::string> ggufs;
    for (auto& entry : fs::directory_iterator(cfg.modelPath, ec)) {
        if (entry.is_regular_file()) {
            std::string extLo = entry.path().extension().string();
            for (auto& c : extLo) c = (char)std::tolower((unsigned char)c);
            if (extLo == ".gguf") ggufs.push_back(entry.path().string());
        }
    }
    if (ec) {
        spdlog::error("MoE: cannot read model directory '{}': {}", cfg.modelPath, ec.message());
        return false;
    }
    std::sort(ggufs.begin(), ggufs.end());
    if (ggufs.empty()) {
        spdlog::error("MoE: no .gguf files found in '{}'", cfg.modelPath);
        return false;
    }
    shard1 = ggufs[0];
    if (ggufs.size() >= 2) shard2 = ggufs[1];

    // Check if native_experts.txt is present (required for i-quant packs)
    bool hasNativeExperts = fs::exists(fs::path(cfg.modelPath) / "native_experts.txt");

    spdlog::info("MoE: shard1={}", shard1);
    if (!shard2.empty()) spdlog::info("MoE: shard2={}", shard2);
    spdlog::info("MoE: native_experts.txt={}", hasNativeExperts ? "found" : "not found");

    std::ostringstream cmd;
    cmd << "\"" << bin << "\"";
    // Always pass --pack so engine can find native_experts.txt
    cmd << " --pack \"" << cfg.modelPath << "\"";
    cmd << " --native \"" << shard1 << "\"";
    if (!shard2.empty())
        cmd << " --ple-gguf \"" << shard2 << "\"";
    cmd << " --serve";
    if (hasNativeExperts) {
        // Native IQ packs: --spec 2 + profile-filled VRAM expert cache.
        // q4_0 KV at 32768 ctx = ~1.5 GB; leaves ~2.5 GB free on 11 GB VRAM.
        int serveCtx = std::min(cfg.contextLen, 32768);
        std::string profilePath = cfg.modelPath + "/expert-profile.bin";
        cmd << " --max-context " << serveCtx
            << " --spec 2 --kv q4_0 --prefill 8192"
            << " --expert-cache 128 --expert-profile \"" << profilePath << "\"";
    } else {
        cmd << " --max-context " << cfg.contextLen;
        if (cfg.prefillAuto) cmd << " --prefill auto";
    }
    if (cfg.gpuIndex >= 0) cmd << " --gpu " << cfg.gpuIndex;
    if (!cfg.extraArgs.empty()) cmd << " " << cfg.extraArgs;
    std::string cmdline = cmd.str();

    spdlog::info("MoE: starting engine: {}", cmdline);

#if defined(_WIN32)
    // Create stdin/stdout pipes for the child
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hChildStdinR = nullptr, hChildStdoutW = nullptr;
    if (!CreatePipe(&hChildStdinR, &hStdinW_, &sa, 0)) { spdlog::error("MoE: CreatePipe stdin failed"); return false; }
    if (!CreatePipe(&hStdoutR_, &hChildStdoutW, &sa, 0)) { spdlog::error("MoE: CreatePipe stdout failed"); return false; }
    // Ensure our write/read ends are not inherited
    SetHandleInformation(hStdinW_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdoutR_, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = hChildStdinR;
    si.hStdOutput = hChildStdoutW;
    // Preserve native diagnostics without mixing them into the protocol pipe.
    si.hStdError = CreateFileA("moe-engine-stderr.log", FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (si.hStdError == INVALID_HANDLE_VALUE)
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    std::string mutableCmd = cmdline;
    // Set working dir to model path's parent if it makes sense
    std::string workDir = fs::path(cfg.modelPath).parent_path().string();

    BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr,
                              TRUE, 0, nullptr,
                              workDir.empty() ? nullptr : workDir.c_str(),
                              &si, &pi);
    // Close child ends on our side
    if (hChildStdinR)  CloseHandle(hChildStdinR);
    if (hChildStdoutW) CloseHandle(hChildStdoutW);
    if (si.hStdError != GetStdHandle(STD_ERROR_HANDLE)) CloseHandle(si.hStdError);
    if (!ok) {
        spdlog::error("MoE: CreateProcess failed ({}): {}", GetLastError(), cmdline);
        return false;
    }
    hProcess_ = pi.hProcess;
    hThread_  = pi.hThread;
    childPid_ = static_cast<int>(pi.dwProcessId);
#else
    int stdinPipe[2], stdoutPipe[2];
    if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0) {
        spdlog::error("MoE: pipe failed"); return false;
    }
    pid_t pid = fork();
    if (pid < 0) { spdlog::error("MoE: fork failed"); return false; }
    if (pid == 0) {
        // child
        close(stdinPipe[1]);  close(stdoutPipe[0]);
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        if (!cfg.modelPath.empty()) {
            fs::path wd = fs::path(cfg.modelPath).parent_path();
            if (fs::exists(wd)) chdir(wd.string().c_str());
        }
        execl("/bin/sh", "sh", "-c", cmdline.c_str(), nullptr);
        _exit(127);
    }
    close(stdinPipe[0]); close(stdoutPipe[1]);
    stdinFd_  = stdinPipe[1];
    stdoutFd_ = stdoutPipe[0];
    childPid_ = pid;
#endif

    running_ = true;
    spdlog::info("MoE: engine started (pid {}), waiting for INFO ready...", childPid_);

    // Engine emits an INFO line when the model is loaded and ready.
    // Wait up to 300 s for that line (mmap-mode loading of a 52 GB IQ3_S pack can take 2+ min);
    // quit early if process dies or ERR is seen.
    bool engineReady = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (std::chrono::steady_clock::now() < deadline) {
        std::string line;
        if (!readLine(line, 5000)) {
#if defined(_WIN32)
            DWORD code = 0;
            if (GetExitCodeProcess(static_cast<HANDLE>(hProcess_), &code) &&
                code != STILL_ACTIVE) {
                spdlog::error("MoE: engine exited early (code={})", code);
                break;
            }
#else
            int st = 0;
            if (waitpid(childPid_, &st, WNOHANG) > 0) {
                spdlog::error("MoE: engine exited early"); break;
            }
#endif
            continue;
        }
        if (line.rfind("READY", 0) == 0) {
            spdlog::info("MoE: engine ready - {}", line.substr(0, 100));
            engineReady = true;
            break;
        }
        if (line.rfind("INFO", 0) == 0) {
            spdlog::info("MoE init: {}", line);
            continue;
        }
        if (line.rfind("ERR", 0) == 0) {
            spdlog::error("MoE: engine error: {}", line); break;
        }
        spdlog::debug("MoE init: {}", line);
    }
    if (!engineReady) {
        spdlog::error("MoE: engine did not become ready within 300s");
        stop(); return false;
    }
    return true;
}

// ---- stop ------------------------------------------------------------------

void MoEEngine::stop() {
    if (!running_) return;
    spdlog::info("MoE: stopping engine (pid {})", childPid_);
#if defined(_WIN32)
    if (hStdinW_)  { CloseHandle(hStdinW_);  hStdinW_  = nullptr; }
    if (hStdoutR_) { CloseHandle(hStdoutR_); hStdoutR_ = nullptr; }
    if (hProcess_) {
        TerminateProcess(static_cast<HANDLE>(hProcess_), 0);
        WaitForSingleObject(static_cast<HANDLE>(hProcess_), 3000);
        CloseHandle(static_cast<HANDLE>(hProcess_));
        CloseHandle(static_cast<HANDLE>(hThread_));
        hProcess_ = nullptr;
        hThread_  = nullptr;
    }
#else
    if (stdinFd_  >= 0) { CLOSE_FD(stdinFd_);  stdinFd_  = -1; }
    if (stdoutFd_ >= 0) { CLOSE_FD(stdoutFd_); stdoutFd_ = -1; }
    if (childPid_ > 0) {
        kill(childPid_, SIGTERM);
        int status = 0;
        waitpid(childPid_, &status, 0);
    }
#endif
    running_ = false;
    childPid_ = 0;
}

// ---- I/O -------------------------------------------------------------------

bool MoEEngine::writeToEngine(const void* data, size_t len) {
#if defined(_WIN32)
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(hStdinW_), data,
                   static_cast<DWORD>(len), &written, nullptr))
        return false;
    return written == len;
#else
    ssize_t n = WRITE_FD(stdinFd_, data, len);
    return n == static_cast<ssize_t>(len);
#endif
}

bool MoEEngine::readLine(std::string& out, int timeoutMs) {
    out.clear();
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
#if defined(_WIN32)
    char c;
    DWORD bytesRead = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        // Peek first to avoid blocking
        DWORD avail = 0;
        if (!PeekNamedPipe(static_cast<HANDLE>(hStdoutR_), nullptr, 0,
                           nullptr, &avail, nullptr)) return false;
        if (avail == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (!ReadFile(static_cast<HANDLE>(hStdoutR_), &c, 1, &bytesRead, nullptr)
            || bytesRead == 0)
            return false;
        if (c == '\n') return true;
        out += c;
    }
    return false;
#else
    char c;
    while (std::chrono::steady_clock::now() < deadline) {
        ssize_t n = READ_FD(stdoutFd_, &c, 1);
        if (n == 1) {
            if (c == '\n') return true;
            out += c;
        } else if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        } else {
            return false;  // EOF
        }
    }
    return false;
#endif
}

// ---- generate --------------------------------------------------------------

bool MoEEngine::generate(const std::vector<int32_t>& promptIds,
                          int maxTokens, float temperature,
                          TokenCallback cb) {
    if (!running_) {
        spdlog::error("MoE: generate called but engine not running");
        return false;
    }
    // Engine serve-mode protocol (from src/program/generate.cpp):
    //   Request:  GEN <max_new> [key=val ...] <id,id,id,...>\n
    //             optional keys: temperature=F top_p=F top_k=N seed=N
    //   Response: T <id>\n  per generated token
    //             DONE <produced> <prompt_n> <prompt_ms> <decode_ms> <finish> ...\n
    //             ERR <msg>\n  on error
    //   Control:  STOP\n (cancel current request)  QUIT\n (end engine)
    std::ostringstream req;
    req << "GEN " << maxTokens;
    if (temperature > 0.0f) req << " temperature=" << temperature;
    req << " top_k=20";
    // Prompt token ids as comma-separated list
    req << " ";
    for (size_t i = 0; i < promptIds.size(); ++i) {
        if (i) req << ",";
        req << promptIds[i];
    }
    req << "\n";
    std::string reqStr = req.str();
    if (!writeToEngine(reqStr.data(), reqStr.size())) {
        spdlog::error("MoE: failed to write GEN request");
        return false;
    }

    // Read generated tokens until DONE or ERR
    int count = 0;
    while (count < maxTokens + 100) {
        std::string line;
        if (!readLine(line, 120000)) {   // 2 min per-token timeout
            spdlog::warn("MoE: read timeout after {} tokens", count);
            // A timed-out response must never become the next request's output.
            stop();
            return false;
        }
        // Skip INFO lines (engine startup) and empty lines
        if (line.empty() || line.rfind("INFO", 0) == 0) continue;
        // DONE <produced> <prompt_n> <prompt_ms> <decode_ms> <finish> ...
        if (line.rfind("DONE", 0) == 0) {
            if (cb) cb(-1, true);
            return true;
        }
        // ERR <msg>
        if (line.rfind("ERR", 0) == 0) {
            spdlog::error("MoE engine: {}", line);
            return false;
        }
        // T <id>  — a generated token
        if (line.rfind("T ", 0) == 0) {
            try {
                int32_t id = std::stoi(line.substr(2));
                if (cb) cb(id, false);
                count++;
            } catch (...) {
                spdlog::debug("MoE: non-integer T line: {}", line);
            }
            continue;
        }
        // YIELDED, VRAM, BDONE, BYIELD — internal control lines, skip
        spdlog::debug("MoE: control line: {}", line);
    }
    spdlog::error("MoE: generation exceeded token response limit");
    return false;
}

// ---- tokenize / detokenize -------------------------------------------------

std::vector<int32_t> MoEEngine::tokenize(const std::string& text) {
    // The engine exposes a tokenizer via its own CLI. For simplicity, the
    // MoELLMClient uses a bundled Qwen BPE tokenizer (src/moe/MoETokenizer.cpp)
    // rather than round-tripping through the engine for every prompt. This
    // stub is kept for parity but MoELLMClient doesn't depend on it.
    (void)text;
    return {};
}

std::string MoEEngine::detokenize(const std::vector<int32_t>& ids) {
    (void)ids;
    return "";
}

} // namespace closecrab::moe
