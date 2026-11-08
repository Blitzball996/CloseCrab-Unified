#pragma once
// MoEHardwareProbe.h — detect GPU/RAM/disk for local MoE model selection

#include <string>
#include <vector>
#include <cstdint>

namespace closecrab::moe {

struct GpuInfo {
    std::string name;
    double      vramGb = 0;
    std::string vendor;
    std::string arch;
    bool        usable = true;
};

struct HardwareInfo {
    std::vector<GpuInfo> gpus;
    double  ramGb       = 0;
    double  diskFreeGb  = 0;
    std::string summary;
    std::string os;
    bool    hasAvx2     = false;
    bool    hasAvx512   = false;
};

// One downloadable shard of a model pack.
struct ModelShard {
    std::string hfRepo;     // "ISTA-DASLab/Qwen3.8-Flash-Next-GSQ-RCO-GGUF"
    std::string hfFile;     // "IQ3_XXS/IQ3_XXS-00001-of-00008.gguf"
    double      sizeGb = 0; // approximate shard size
    std::string sha256;     // optional SHA-256 hex (empty = skip check)
};

struct ModelEntry {
    std::string family;
    std::string quant;
    std::string id;
    std::string description;
    double downloadGb  = 0;
    double memoryGb    = 0;
    double minVramGb   = 0;
    double minRamGb    = 0;
    // Full shard list (populated by modelCatalog)
    std::vector<ModelShard> shards;
    bool   recommended = false;
    std::string verdict;
    std::string verdictWords;
};

HardwareInfo detectHardware(const std::string& modelDrive = "");
ModelEntry   fitForHardware(ModelEntry entry, const HardwareInfo& hw);
std::vector<ModelEntry> modelCatalog(const HardwareInfo& hw);
const ModelEntry* recommendModel(const std::vector<ModelEntry>& catalog);
std::string hwSummary(const HardwareInfo& hw);

} // namespace closecrab::moe
