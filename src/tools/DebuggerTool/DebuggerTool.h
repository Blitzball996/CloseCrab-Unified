#pragma once
#include "../Tool.h"
#include <sstream>
#include <iomanip>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

namespace closecrab {

class DebuggerTool : public Tool {
public:
    std::string getName() const override { return "Debugger"; }
    std::string getDescription() const override {
        return "Native process debugger. Actions: list_processes, read_memory, list_threads, "
               "list_modules, search_memory. Attach to running processes for inspection.";
    }
    std::string getCategory() const override { return "debug"; }
    bool isDestructive() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"action", {{"type", "string"},
                    {"enum", {"list_processes", "read_memory", "list_threads", "list_modules", "search_memory"}}}},
                {"pid", {{"type", "integer"}, {"description", "Process ID"}}},
                {"address", {{"type", "integer"}, {"description", "Memory address to read"}}},
                {"size", {{"type", "integer"}, {"description", "Bytes to read (default 64)"}}},
                {"pattern", {{"type", "string"}, {"description", "String pattern to search in memory"}}}
            }},
            {"required", {"action"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
#ifdef _WIN32
        std::string action = input["action"].get<std::string>();
        if (action == "list_processes") return listProcesses();
        if (action == "read_memory") {
            int pid = input.value("pid", 0);
            uint64_t addr = input.value("address", (uint64_t)0);
            int size = input.value("size", 64);
            return readMemory(pid, addr, size);
        }
        if (action == "list_threads") {
            int pid = input.value("pid", 0);
            return listThreads(pid);
        }
        if (action == "list_modules") {
            int pid = input.value("pid", 0);
            return listModules(pid);
        }
        if (action == "search_memory") {
            int pid = input.value("pid", 0);
            std::string pattern = input.value("pattern", "");
            return searchMemory(pid, pattern);
        }
#endif
        return ToolResult::fail("Unsupported action or platform");
    }

private:
#ifdef _WIN32
    ToolResult listProcesses() {
        std::ostringstream oss;
        oss << "Running processes:\n\n";
        oss << std::left << std::setw(8) << "PID" << std::setw(40) << "Name" << "Memory\n";
        oss << std::string(60, '-') << "\n";

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return ToolResult::fail("Cannot enumerate processes");

        PROCESSENTRY32 pe = {}; pe.dwSize = sizeof(pe);
        int count = 0;
        if (Process32First(snap, &pe)) {
            do {
                if (count++ > 50) { oss << "... (truncated)\n"; break; }
                oss << std::left << std::setw(8) << pe.th32ProcessID
                    << std::setw(40) << pe.szExeFile << "\n";
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
        return ToolResult::ok(oss.str());
    }

    ToolResult readMemory(int pid, uint64_t address, int size) {
        if (pid == 0) return ToolResult::fail("pid required");
        if (size > 4096) size = 4096;

        HANDLE proc = OpenProcess(PROCESS_VM_READ, FALSE, pid);
        if (!proc) return ToolResult::fail("Cannot open process " + std::to_string(pid) + " (access denied?)");

        std::vector<uint8_t> buffer(size);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(proc, (LPCVOID)address, buffer.data(), size, &bytesRead)) {
            CloseHandle(proc);
            return ToolResult::fail("Cannot read memory at 0x" + toHex(address));
        }
        CloseHandle(proc);

        // Format as hex dump
        std::ostringstream oss;
        oss << "Memory at 0x" << toHex(address) << " (" << bytesRead << " bytes, PID " << pid << "):\n\n";
        for (size_t i = 0; i < bytesRead; i += 16) {
            oss << toHex(address + i) << "  ";
            for (size_t j = 0; j < 16 && i+j < bytesRead; j++) {
                oss << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i+j] << " ";
            }
            oss << " |";
            for (size_t j = 0; j < 16 && i+j < bytesRead; j++) {
                char c = buffer[i+j];
                oss << (c >= 32 && c < 127 ? c : '.');
            }
            oss << "|\n";
        }
        return ToolResult::ok(oss.str());
    }

    ToolResult listThreads(int pid) {
        if (pid == 0) return ToolResult::fail("pid required");
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return ToolResult::fail("Cannot enumerate threads");

        std::ostringstream oss;
        oss << "Threads for PID " << pid << ":\n\n";
        THREADENTRY32 te = {}; te.dwSize = sizeof(te);
        int count = 0;
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID == (DWORD)pid) {
                    oss << "  Thread " << te.th32ThreadID << " (priority: " << te.tpBasePri << ")\n";
                    count++;
                }
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
        oss << "\nTotal: " << count << " threads\n";
        return ToolResult::ok(oss.str());
    }

    ToolResult listModules(int pid) {
        if (pid == 0) return ToolResult::fail("pid required");
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) return ToolResult::fail("Cannot enumerate modules (access denied?)");

        std::ostringstream oss;
        oss << "Modules for PID " << pid << ":\n\n";
        oss << std::left << std::setw(20) << "Base" << std::setw(10) << "Size" << "Name\n";
        oss << std::string(70, '-') << "\n";

        MODULEENTRY32 me = {}; me.dwSize = sizeof(me);
        if (Module32First(snap, &me)) {
            do {
                oss << "0x" << std::hex << std::setw(16) << std::setfill('0') << (uint64_t)me.modBaseAddr
                    << std::dec << std::setfill(' ') << "  " << std::setw(10) << me.modBaseSize
                    << me.szModule << "\n";
            } while (Module32Next(snap, &me));
        }
        CloseHandle(snap);
        return ToolResult::ok(oss.str());
    }

    ToolResult searchMemory(int pid, const std::string& pattern) {
        if (pid == 0) return ToolResult::fail("pid required");
        if (pattern.empty()) return ToolResult::fail("pattern required");

        HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!proc) return ToolResult::fail("Cannot open process");

        std::ostringstream oss;
        oss << "Searching for \"" << pattern << "\" in PID " << pid << ":\n\n";

        MEMORY_BASIC_INFORMATION mbi;
        uint8_t* addr = nullptr;
        int found = 0;
        while (VirtualQueryEx(proc, addr, &mbi, sizeof(mbi)) && found < 20) {
            if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ))) {
                std::vector<uint8_t> buf(mbi.RegionSize);
                SIZE_T read;
                if (ReadProcessMemory(proc, addr, buf.data(), mbi.RegionSize, &read)) {
                    for (size_t i = 0; i + pattern.size() <= read; i++) {
                        if (memcmp(&buf[i], pattern.c_str(), pattern.size()) == 0) {
                            oss << "  Found at 0x" << toHex((uint64_t)addr + i) << "\n";
                            found++;
                            if (found >= 20) break;
                        }
                    }
                }
            }
            addr += mbi.RegionSize;
        }
        CloseHandle(proc);
        oss << "\n" << found << " matches found\n";
        return ToolResult::ok(oss.str());
    }

    static std::string toHex(uint64_t val) {
        std::ostringstream oss;
        oss << std::hex << std::setw(16) << std::setfill('0') << val;
        return oss.str();
    }
#endif
};

} // namespace closecrab
