#pragma once
#include "../Tool.h"
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace closecrab {

class SystemInfoTool : public Tool {
public:
    std::string getName() const override { return "SystemInfo"; }
    std::string getDescription() const override {
        return "Get system hardware info: CPU, memory, GPU, disk. "
               "Useful for performance optimization recommendations.";
    }
    std::string getCategory() const override { return "system"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"category", {{"type", "string"},
                    {"enum", {"all", "cpu", "memory", "gpu", "disk"}},
                    {"description", "Info category (default: all)"}}}
            }}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string category = input.value("category", "all");
        std::ostringstream oss;

#ifdef _WIN32
        if (category == "all" || category == "cpu") {
            SYSTEM_INFO si; GetSystemInfo(&si);
            oss << "CPU:\n";
            oss << "  Processors: " << si.dwNumberOfProcessors << "\n";
            oss << "  Architecture: ";
            switch (si.wProcessorArchitecture) {
                case PROCESSOR_ARCHITECTURE_AMD64: oss << "x86-64"; break;
                case PROCESSOR_ARCHITECTURE_ARM64: oss << "ARM64"; break;
                case PROCESSOR_ARCHITECTURE_INTEL: oss << "x86"; break;
                default: oss << "unknown"; break;
            }
            oss << "\n";
            oss << "  Page size: " << si.dwPageSize << " bytes\n\n";
        }

        if (category == "all" || category == "memory") {
            MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
            GlobalMemoryStatusEx(&ms);
            oss << "Memory:\n";
            oss << "  Total: " << (ms.ullTotalPhys / (1024*1024)) << " MB\n";
            oss << "  Available: " << (ms.ullAvailPhys / (1024*1024)) << " MB\n";
            oss << "  Usage: " << ms.dwMemoryLoad << "%\n";
            oss << "  Virtual total: " << (ms.ullTotalVirtual / (1024*1024)) << " MB\n\n";
        }

        if (category == "all" || category == "disk") {
            oss << "Disk:\n";
            // Enumerate all fixed drives
            DWORD drives = GetLogicalDrives();
            char driveLetter[] = "A:\\";
            for (int i = 0; i < 26; i++) {
                if (drives & (1 << i)) {
                    driveLetter[0] = 'A' + i;
                    UINT type = GetDriveTypeA(driveLetter);
                    if (type == DRIVE_FIXED) {
                        ULARGE_INTEGER freeBytes, totalBytes;
                        if (GetDiskFreeSpaceExA(driveLetter, &freeBytes, &totalBytes, nullptr)) {
                            oss << "  " << driveLetter << "\n";
                            oss << "    Total: " << (totalBytes.QuadPart / (1024*1024*1024)) << " GB\n";
                            oss << "    Free: " << (freeBytes.QuadPart / (1024*1024*1024)) << " GB\n";
                        }
                    }
                }
            }
            oss << "\n";
        }

        if (category == "all" || category == "gpu") {
            oss << "GPU:\n";
            oss << "  (Use 'Bash' tool with 'wmic path win32_VideoController get Name' for GPU details)\n\n";
        }
#endif
        if (oss.str().empty()) return ToolResult::fail("No info available for this platform");
        return ToolResult::ok(oss.str());
    }
};

} // namespace closecrab