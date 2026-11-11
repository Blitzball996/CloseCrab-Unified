// MoEHardwareProbe.cpp — C++ port of Strata's hardware detection and model fit.
// Replaces strata_cli/launch/catalog.py (hardware(), fit_on(), verdict(), recommend())
// with pure C++ so CloseCrab needs no Python runtime for MoE model selection.
// Fit arithmetic mirrors catalog.py fit_on() and verdict() exactly.

#include "MoEHardwareProbe.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <sys/statvfs.h>
#  include <sys/utsname.h>
#else
#  include <sys/statvfs.h>
#  include <sys/utsname.h>
#  include <fstream>
#endif

namespace closecrab::moe {

// ---- helpers ---------------------------------------------------------------

static std::string runCapture(const std::string& cmd) {
#if defined(_WIN32)
    FILE* fp = _popen(cmd.c_str(), "r");
#else
    FILE* fp = popen(cmd.c_str(), "r");
#endif
    if (!fp) return "";
    std::string out;
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), fp)) out += buf.data();
#if defined(_WIN32)
    _pclose(fp);
#else
    pclose(fp);
#endif
    return out;
}

static std::string trim(std::string s) {
    auto notSpace = [](char c){ return !std::isspace((unsigned char)c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

// ---- memory ----------------------------------------------------------------

static double systemRamGb() {
#if defined(_WIN32)
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st))
        return static_cast<double>(st.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
#elif defined(__APPLE__)
    uint64_t bytes = 0;
    size_t sz = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &sz, nullptr, 0) == 0)
        return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
#else
    std::ifstream f("/proc/meminfo");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            long long kb = 0;
            if (sscanf(line.c_str(), "MemTotal: %lld kB", &kb) == 1)
                return static_cast<double>(kb) / (1024.0 * 1024.0);
        }
    }
#endif
    return 0.0;
}

// ---- disk free -------------------------------------------------------------

static double diskFreeGb(const std::string& drive) {
#if defined(_WIN32)
    std::wstring path = drive.empty() ? L"C:\\" : std::wstring(drive.begin(), drive.end());
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L"\\";
    ULARGE_INTEGER avail{};
    if (GetDiskFreeSpaceExW(path.c_str(), &avail, nullptr, nullptr))
        return static_cast<double>(avail.QuadPart) / (1024.0 * 1024.0 * 1024.0);
#elif defined(__APPLE__)
    struct statvfs sv{};
    if (statvfs(drive.empty() ? "/" : drive.c_str(), &sv) == 0)
        return static_cast<double>(sv.f_bavail * sv.f_frsize) / (1024.0 * 1024.0 * 1024.0);
#else
    struct statvfs sv{};
    if (statvfs(drive.empty() ? "/" : drive.c_str(), &sv) == 0)
        return static_cast<double>(sv.f_bavail * sv.f_frsize) / (1024.0 * 1024.0 * 1024.0);
#endif
    return 0.0;
}

// ---- GPU detection ---------------------------------------------------------

static std::vector<GpuInfo> detectNvidia() {
    std::vector<GpuInfo> gpus;
    auto path = std::getenv("PATH");
    // Try nvidia-smi
    std::string out = runCapture("nvidia-smi --query-gpu=index,name,memory.total,driver_version,compute_cap "
                                 "--format=csv,noheader,nounits 2>nul");
    if (out.empty()) {
#ifdef _WIN32
        // Try common install path
        out = runCapture("\"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvidia-smi.exe\" "
                         "--query-gpu=index,name,memory.total,driver_version,compute_cap "
                         "--format=csv,noheader,nounits");
#endif
    }
    if (out.empty()) return gpus;

    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        // "0, NVIDIA GeForce RTX 3060, 12288, 560.94, 8.6"
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream ls(line);
        int idx; std::string name, dr;
        long memTotal; double cc;
        ls >> idx >> std::ws;
        // name may contain spaces — read rest of comma-separated tokens
        std::getline(ls, name, ' '); name = trim(name);
        // Hmm, names have spaces. Read until we hit a number.
        // Simpler: parse by splitting on commas in original.
        break;
    }
    // Re-parse with comma splitting
    gpus.clear();
    ss.clear(); ss.str(out);
    while (std::getline(ss, line)) {
        // split by ", "
        std::vector<std::string> parts;
        std::string cur;
        for (char c : line) {
            if (c == ',') { parts.push_back(trim(cur)); cur.clear(); }
            else cur += c;
        }
        parts.push_back(trim(cur));
        if (parts.size() < 5) continue;
        GpuInfo g;
        g.name    = parts[1];
        // "12288" -> GB
        try { g.vramGb = std::stod(parts[2]) / 1024.0; } catch (...) {}
        g.vendor  = "nvidia";
        // compute_cap like "8.6" -> arch "86"
        std::string cc = parts[4];
        cc.erase(std::remove(cc.begin(), cc.end(), '.'), cc.end());
        g.arch    = cc;
        g.usable  = !g.name.empty() && g.vramGb > 0;
        gpus.push_back(g);
    }
    return gpus;
}

#ifdef _WIN32
static std::vector<GpuInfo> detectWindowsAdapters() {
    std::vector<GpuInfo> gpus;
    // DXGI adapter enumeration would need dxgi.h + linkage; fall back to
    // reading the registry's Display adapters. Names only (no VRAM here).
    HKEY hKey{};
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return gpus;
    DWORD idx = 0;
    char sub[256];
    DWORD subSz = sizeof(sub);
    while (RegEnumKeyExA(hKey, idx++, sub, &subSz, nullptr, nullptr, nullptr, 0) == ERROR_SUCCESS) {
        subSz = sizeof(sub);
        // Look for "VEN_10DE" (NVIDIA), "VEN_1002" (AMD)
        std::string s(sub);
        if (s.find("VEN_10DE") != std::string::npos ||
            s.find("VEN_1002") != std::string::npos ||
            s.find("VEN_8086") != std::string::npos) {
            // Walk down to find friendly name
            HKEY hSub{};
            std::string path = std::string(sub) + "\\Device Parameters";
            // This is a best-effort probe; the nvidia-smi path is authoritative
        }
    }
    RegCloseKey(hKey);
    return gpus;  // usually empty — nvidia-smi wins
}
#endif

// ---- AVX detection ---------------------------------------------------------

static bool cpuHasAvx2() {
#if defined(_WIN32)
    return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0;
#elif defined(__aarch64__) || defined(__arm64__)
    return false;
#elif defined(__AVX2__)
    return true;
#else
    unsigned int eax, ebx, ecx, edx;
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    return (ebx & (1u << 5)) != 0;  // AVX2 bit
#endif
}

static bool cpuHasAvx512() {
#if defined(_WIN32)
    return IsProcessorFeaturePresent(PF_AVX512F_INSTRUCTIONS_AVAILABLE) != 0;
#elif defined(__aarch64__) || defined(__arm64__)
    return false;
#elif defined(__AVX512F__)
    return true;
#else
    unsigned int eax, ebx, ecx, edx;
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    return (ebx & (1u << 16)) != 0;  // AVX-512F bit
#endif
}

// ---- public API ------------------------------------------------------------

HardwareInfo detectHardware(const std::string& modelDrive) {
    HardwareInfo hw;
    hw.ramGb      = systemRamGb();
    // Default: check the drive of the current working directory (where models
    // will actually be saved), not always C: which may be a small system drive.
    std::string drive = modelDrive;
    if (drive.empty()) {
        std::error_code ec;
        auto cwd = std::filesystem::current_path(ec);
        if (!ec) drive = cwd.string();
    }
    hw.diskFreeGb = diskFreeGb(drive.empty() ? "C:/" : drive);
    hw.gpus       = detectNvidia();
    if (hw.gpus.empty()) {
#ifdef _WIN32
        hw.gpus = detectWindowsAdapters();
#endif
    }
#if defined(_WIN32)
    hw.os = "Windows";
#elif defined(__APPLE__)
    hw.os = "macOS";
#else
    hw.os = "Linux";
#endif
    hw.hasAvx2   = cpuHasAvx2();
    hw.hasAvx512 = cpuHasAvx512();
    hw.summary   = hwSummary(hw);
    return hw;
}

std::string hwSummary(const HardwareInfo& hw) {
    std::ostringstream s;
    if (!hw.gpus.empty()) {
        auto& g = hw.gpus[0];
        std::string shortName = g.name;
        for (auto pre : {"NVIDIA GeForce ", "AMD Radeon ", "Intel "}) {
            auto p = shortName.find(pre);
            if (p == 0) { shortName.erase(0, std::strlen(pre)); break; }
        }
        s << shortName << " " << static_cast<int>(g.vramGb) << " GB · ";
    }
    if (hw.ramGb > 0) s << static_cast<int>(hw.ramGb) << " GB RAM · ";
    if (hw.diskFreeGb > 0) s << static_cast<int>(hw.diskFreeGb) << " GB free";
    return trim(s.str());
}

// ---- fit (mirrors catalog.py fit_on + verdict) -----------------------------
// memoryGb is the total RAM+VRAM the model needs, from Strata's MODELS.md.
// We compare against available = VRAM + (RAM - system reserve).
static constexpr double SYSTEM_ROOM_GB = 10.0;  // RAM reserved for OS + app

ModelEntry fitForHardware(ModelEntry e, const HardwareInfo& hw) {
    if (hw.gpus.empty() || hw.ramGb <= 0) {
        e.verdict = "tight";
        e.verdictWords = "not checked (no GPU/RAM detected)";
        return e;
    }
    double vram = hw.gpus[0].vramGb;
    double ramRoom = std::max(0.0, hw.ramGb - SYSTEM_ROOM_GB);
    double available = vram + ramRoom;

    double slack = available - e.memoryGb;
    if (slack < 0) {
        // Doesn't fit in RAM+VRAM; experts spill to SSD (very slow)
        double spill = -slack;
        if (spill > e.memoryGb * 0.2) {
            e.verdict = "no";    e.verdictWords = "too big — too much SSD reading, very slow";
        } else {
            e.verdict = "tight"; e.verdictWords = "tight: some experts read from SSD";
        }
    } else if (slack < e.memoryGb * 0.15) {
        e.verdict = "fits";  e.verdictWords = "fits — close other programs for best speed";
    } else {
        e.verdict = "fits";  e.verdictWords = "fits";
    }
    // Disk check
    if (e.downloadGb > 0 && hw.diskFreeGb > 0 && e.downloadGb > hw.diskFreeGb) {
        e.verdict = "no"; e.verdictWords = "not enough free disk space";
    }
    return e;
}

// ---- built-in model catalog (Qwen3.8-Flash-Next family) --------------------
// memoryGb is the total memory (experts + dense) at each quant.
// Shards: ISTA-DASLab packs are split GGUF files; counts from MODELS.md.

static ModelEntry makeEntry(const char* family, const char* quant,
                            const char* id, const char* desc,
                            double dlGb, double memGb,
                            double minVram, double minRam,
                            const char* hfRepo, int nShards,
                            const char* prefix, const char* suffix)
{
    ModelEntry e;
    e.family = family; e.quant = quant; e.id = id; e.description = desc;
    e.downloadGb = dlGb; e.memoryGb = memGb;
    e.minVramGb = minVram; e.minRamGb = minRam;
    double perShard = dlGb / std::max(1, nShards);
    if (nShards == 1) {
        // Single-file model (e.g. Unsloth flat GGUF): filename is just prefix+suffix
        ModelShard s;
        s.hfRepo = hfRepo;
        s.hfFile = std::string(prefix) + suffix;
        s.sizeGb = dlGb;
        e.shards.push_back(s);
    } else {
        for (int i = 1; i <= nShards; ++i) {
            char idx[16];
            std::snprintf(idx, sizeof idx, "%05d", i);
            ModelShard s;
            s.hfRepo = hfRepo;
            s.hfFile = std::string(prefix) + std::string(idx) + "-of-" +
                       std::to_string(nShards) + suffix;
            s.sizeGb = perShard;
            e.shards.push_back(s);
        }
    }
    return e;
}

static std::vector<ModelEntry> baseCatalog() {
    // Source: Strata MODELS.md reviewed 2026-10-25 v0.2.5
    // All ISTA-DASLab packs = 2 shards: shard1=experts, shard2=n-gram table (~28 GB on SSD)
    // memoryGb = RAM+VRAM needed (from "Pick by memory" table in MODELS.md)
    // downloadGb = shard1 + shard2 combined
    // Filenames confirmed from bench/results READMEs
    const char* repo = "ISTA-DASLab/Qwen3.8-Flash-Next-GSQ-RCO-GGUF";
    return {
        makeEntry("qwen","Q2_0","qwen-q2_0",
            "Fastest. 37.6 GB RAM+VRAM needed.",
            62.0, 37.6, 6.0, 32.0, repo, 2,
            "Q2_0/Qwen3.8-Flash-Next-GSQ-RCO-Q2_0-0000", "-of-00002.gguf"),
        makeEntry("qwen","IQ2_XS","qwen-iq2_xs",
            "Fast, better quality. 39.2 GB RAM+VRAM needed.",
            64.5, 39.2, 6.0, 34.0, repo, 2,
            "IQ2_XS/Qwen3.8-Flash-Next-GSQ-RCO-IQ2_XS-0000", "-of-00002.gguf"),
        makeEntry("qwen","IQ3_XXS","qwen-iq3_xxs",
            "Great quality. 47.0 GB RAM+VRAM needed.",
            76.0, 47.0, 8.0, 42.0, repo, 2,
            "IQ3_XXS/Qwen3.8-Flash-Next-GSQ-RCO-IQ3_XXS-0000", "-of-00002.gguf"),
        makeEntry("qwen","IQ3_S","qwen-iq3_s",
            "Best ISTA quality. 54.8 GB RAM+VRAM. Matches full model on tests.",
            83.6, 54.8, 8.0, 48.0, repo, 2,
            "IQ3_S/Qwen3.8-Flash-Next-GSQ-RCO-IQ3_S-0000", "-of-00002.gguf"),
        makeEntry("qwen","Coder","qwen-coder",
            "Code-tuned, half the experts. 32 GB RAM+VRAM. Weaker on general text.",
            58.0, 32.0, 4.0, 28.0,
            "ISTA-DASLab/Qwen3.8-Flash-Next-GSQ-RCO-Coder-GGUF", 2,
            "Coder/Qwen3.8-Flash-Next-GSQ-RCO-Coder-IQ1_M-0000", "-of-00002.gguf"),
    };
}

std::vector<ModelEntry> modelCatalog(const HardwareInfo& hw) {
    auto cats = baseCatalog();
    for (auto& m : cats) m = fitForHardware(m, hw);
    // Mark recommended: first "fits" by download size ascending
    auto fits = std::stable_partition(cats.begin(), cats.end(),
        [](const ModelEntry& m){ return m.verdict != "fits"; });
    // cats now: [no/tight..., fits...]
    auto recIt = std::min_element(fits, cats.end(),
        [](const ModelEntry& a, const ModelEntry& b){ return a.downloadGb < b.downloadGb; });
    if (recIt != cats.end()) recIt->recommended = true;
    return cats;
}

const ModelEntry* recommendModel(const std::vector<ModelEntry>& cats) {
    auto it = std::find_if(cats.begin(), cats.end(),
                           [](const ModelEntry& m){ return m.recommended; });
    if (it != cats.end()) return &*it;
    it = std::find_if(cats.begin(), cats.end(),
                     [](const ModelEntry& m){ return m.verdict == "fits"; });
    if (it != cats.end()) return &*it;
    return nullptr;
}

} // namespace closecrab::moe
