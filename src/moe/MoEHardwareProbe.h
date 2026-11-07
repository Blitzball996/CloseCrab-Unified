#pragma once
// MoEHardwareProbe.h — detect GPU/RAM/disk for local MoE model selection
// All C++, no Python, no external tools beyond nvidia-smi (optional).
// Used by CloseCrab's /local command and provider="local-moe".

#include <string>
#include <vector>
#include <cstdint>

namespace closecrab::moe {

struct GpuInfo {
    std::string name;       // "NVIDIA GeForce RTX 3060"
    double      vramGb = 0; // usable VRAM in GB
    std::string vendor;     // "nvidia" | "amd" | "intel" | "apple"
    std::string arch;       // SM arch: "86" for Ampere, "89" for Ada, etc.
    bool        usable = true;
};

struct HardwareInfo {
    std::vector<GpuInfo> gpus;
    double  ramGb       = 0;  // total system RAM in GB
    double  diskFreeGb  = 0;  // free disk on model drive
    std::string summary;      // "RTX 3060 12 GB · 32 GB RAM · 480 GB free"
    std::string os;           // "Windows" | "Linux" | "macOS"
    bool    hasAvx2     = false;
    bool    hasAvx512   = false;
};

// Model entry from the built-in catalog (no Python needed).
struct ModelEntry {
    std::string family;       // "qwen"
    std::string quant;        // "IQ3_XXS"
    std::string id;           // run-config tag
    std::string description;  // "Fast, good quality"
    double downloadGb  = 0;   // compressed download size
    double memoryGb    = 0;   // total memory needed (GPU+RAM)
    double minVramGb   = 0;   // minimum VRAM to run at useful speed
    double minRamGb    = 0;   // minimum RAM (when vram covers dense)
    std::string hfRepo;       // HuggingFace repo path
    std::string hfFile;       // main filename on HF
    bool   recommended = false;
    // Fit result (filled by fitForHardware)
    std::string verdict;      // "fits" | "tight" | "no"
    std::string verdictWords; // human-readable explanation
};

// Detect hardware (GPU via nvidia-smi + WinAPI/proc, RAM via OS API).
HardwareInfo detectHardware(const std::string& modelDrive = "");

// Compute fit verdict for a model on given hardware.
// Mirrors strata_cli/launch/catalog.py fit_on() + verdict() exactly.
ModelEntry fitForHardware(ModelEntry entry, const HardwareInfo& hw);

// Return the built-in model catalog, with fit verdicts computed for hw.
std::vector<ModelEntry> modelCatalog(const HardwareInfo& hw);

// Return the recommended model for this hardware (best fits/tight entry).
const ModelEntry* recommendModel(const std::vector<ModelEntry>& catalog);

// Format hardware summary string.
std::string hwSummary(const HardwareInfo& hw);

} // namespace closecrab::moe
