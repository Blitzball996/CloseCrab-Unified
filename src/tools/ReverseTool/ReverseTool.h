#pragma once
#include "../Tool.h"
#include "CudaBinaryAnalyzer.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <vector>
#include <capstone/capstone.h>

namespace closecrab {

class ReverseTool : public Tool {
public:
    std::string getName() const override { return "Reverse"; }
    std::string getDescription() const override {
        return "Binary reverse engineering tool. Actions: "
               "hexdump (view raw bytes), "
               "strings (extract text), "
               "headers (PE/ELF file info), "
               "imports (DLL dependencies), "
               "sections (memory layout), "
               "disasm (x86-64 disassembly), "
               "functions (find function entries), "
               "entropy (code/data/encrypted classification), "
               "crypto (detect AES/RC4/packers), "
               "compare (binary similarity).";
    }
    std::string getCategory() const override { return "analysis"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"file_path", {{"type", "string"}, {"description", "Path to binary file"}}},
                {"action", {{"type", "string"},
                    {"enum", {"hexdump", "strings", "headers", "imports", "sections", "disasm", "functions", "entropy", "crypto", "compare"}},
                    {"description", "Analysis action: hexdump, strings, headers, imports, sections, disasm, functions, entropy (CUDA-accelerated block entropy map), crypto (detect encryption/packers), compare (binary similarity)"}}},
                {"offset", {{"type", "integer"}, {"description", "Start offset (default: entry point for disasm, 0 for hexdump)"}}},
                {"length", {{"type", "integer"}, {"description", "Bytes to analyze (default 256 for hexdump, 512 for disasm)"}}},
                {"min_length", {{"type", "integer"}, {"description", "Minimum string length (default 4)"}}},
                {"compare_path", {{"type", "string"}, {"description", "Second binary path (for compare action)"}}}
            }},
            {"required", {"file_path", "action"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string path = input["file_path"].get<std::string>();
        std::string action = input["action"].get<std::string>();

        if (!std::filesystem::exists(path)) {
            return ToolResult::fail("File not found: " + path);
        }

        if (action == "hexdump") {
            int offset = input.value("offset", 0);
            int length = input.value("length", 256);
            return hexDump(path, offset, length);
        } else if (action == "strings") {
            int minLen = input.value("min_length", 4);
            return extractStrings(path, minLen);
        } else if (action == "headers") {
            return parseHeaders(path);
        } else if (action == "imports") {
            return parseImports(path);
        } else if (action == "sections") {
            return parseSections(path);
        } else if (action == "disasm") {
            int offset = input.value("offset", -1); // -1 = auto-detect entry point
            int length = input.value("length", 512);
            return disassemble(path, offset, length);
        } else if (action == "functions") {
            return findFunctions(path);
        } else if (action == "entropy") {
            return analyzeEntropy(path);
        } else if (action == "crypto") {
            return detectCrypto(path);
        } else if (action == "compare") {
            std::string path2 = input.value("compare_path", "");
            if (path2.empty()) return ToolResult::fail("compare_path required for compare action");
            return compareBinaries(path, path2);
        }
        return ToolResult::fail("Unknown action: " + action);
    }

private:
    ToolResult hexDump(const std::string& path, int offset, int length) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open file");

        file.seekg(offset);
        std::vector<uint8_t> buffer(length);
        file.read(reinterpret_cast<char*>(buffer.data()), length);
        int bytesRead = (int)file.gcount();

        std::ostringstream oss;
        oss << "Hex dump of " << path << " (offset " << offset << ", " << bytesRead << " bytes):\n\n";

        for (int i = 0; i < bytesRead; i += 16) {
            oss << std::hex << std::setw(8) << std::setfill('0') << (offset + i) << "  ";
            // Hex bytes
            for (int j = 0; j < 16; j++) {
                if (i + j < bytesRead)
                    oss << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i+j] << " ";
                else
                    oss << "   ";
                if (j == 7) oss << " ";
            }
            oss << " |";
            // ASCII
            for (int j = 0; j < 16 && i + j < bytesRead; j++) {
                char c = buffer[i+j];
                oss << (c >= 32 && c < 127 ? c : '.');
            }
            oss << "|\n";
        }
        return ToolResult::ok(oss.str());
    }

    ToolResult extractStrings(const std::string& path, int minLen) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open file");

        std::ostringstream oss;
        oss << "Strings in " << path << " (min length " << minLen << "):\n\n";

        std::string current;
        int count = 0;
        char c;
        while (file.get(c) && count < 500) {
            if (c >= 32 && c < 127) {
                current += c;
            } else {
                if ((int)current.size() >= minLen) {
                    oss << "  " << current << "\n";
                    count++;
                }
                current.clear();
            }
        }
        if ((int)current.size() >= minLen && count < 500) {
            oss << "  " << current << "\n";
            count++;
        }

        oss << "\n(" << count << " strings found)";
        return ToolResult::ok(oss.str());
    }

    ToolResult parseHeaders(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open file");

        uint8_t magic[4];
        file.read(reinterpret_cast<char*>(magic), 4);

        std::ostringstream oss;
        oss << "File: " << path << "\n";
        oss << "Size: " << std::filesystem::file_size(path) << " bytes\n";

        // PE check (MZ header)
        if (magic[0] == 'M' && magic[1] == 'Z') {
            oss << "Format: PE (Windows Executable)\n";
            // Read PE offset
            file.seekg(0x3C);
            uint32_t peOffset = 0;
            file.read(reinterpret_cast<char*>(&peOffset), 4);
            oss << "PE Header offset: 0x" << std::hex << peOffset << "\n";

            file.seekg(peOffset);
            uint8_t peSig[4];
            file.read(reinterpret_cast<char*>(peSig), 4);
            if (peSig[0] == 'P' && peSig[1] == 'E') {
                oss << "PE Signature: valid\n";
                // COFF header
                uint16_t machine, numSections;
                file.read(reinterpret_cast<char*>(&machine), 2);
                file.read(reinterpret_cast<char*>(&numSections), 2);
                oss << "Machine: 0x" << std::hex << machine;
                if (machine == 0x8664) oss << " (x86-64)";
                else if (machine == 0x14c) oss << " (x86)";
                else if (machine == 0xAA64) oss << " (ARM64)";
                oss << "\nSections: " << std::dec << numSections << "\n";
            }
        }
        // ELF check
        else if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
            oss << "Format: ELF (Linux/Unix Executable)\n";
            uint8_t elfClass;
            file.read(reinterpret_cast<char*>(&elfClass), 1);
            oss << "Class: " << (elfClass == 2 ? "64-bit" : "32-bit") << "\n";
        }
        else {
            oss << "Format: Unknown (magic: ";
            for (int i = 0; i < 4; i++) oss << std::hex << std::setw(2) << std::setfill('0') << (int)magic[i] << " ";
            oss << ")\n";
        }

        return ToolResult::ok(oss.str());
    }

    ToolResult parseImports(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open file");

        uint8_t magic[2];
        file.read(reinterpret_cast<char*>(magic), 2);
        if (magic[0] != 'M' || magic[1] != 'Z') {
            return ToolResult::fail("Not a PE file (no MZ header)");
        }

        // Use strings extraction to find DLL names
        file.seekg(0);
        std::string content((std::istreambuf_iterator<char>(file)), {});
        std::ostringstream oss;
        oss << "Imported DLLs (detected by name pattern):\n\n";

        std::string current;
        int count = 0;
        for (char c : content) {
            if (c >= 32 && c < 127) current += c;
            else {
                if (current.size() > 4 &&
                    (current.find(".dll") != std::string::npos ||
                     current.find(".DLL") != std::string::npos)) {
                    oss << "  " << current << "\n";
                    count++;
                }
                current.clear();
            }
            if (count > 100) break;
        }
        oss << "\n(" << count << " DLL references found)";
        return ToolResult::ok(oss.str());
    }

    ToolResult parseSections(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open file");

        uint8_t magic[2];
        file.read(reinterpret_cast<char*>(magic), 2);
        if (magic[0] != 'M' || magic[1] != 'Z') {
            return ToolResult::fail("Not a PE file");
        }

        file.seekg(0x3C);
        uint32_t peOffset = 0;
        file.read(reinterpret_cast<char*>(&peOffset), 4);

        file.seekg(peOffset + 4); // Skip PE signature
        uint16_t machine, numSections;
        file.read(reinterpret_cast<char*>(&machine), 2);
        file.read(reinterpret_cast<char*>(&numSections), 2);

        // Skip rest of COFF header (16 bytes) + optional header size
        uint32_t timestamp;
        file.read(reinterpret_cast<char*>(&timestamp), 4);
        file.seekg(8, std::ios::cur); // skip symbol table ptr + num symbols
        uint16_t optHeaderSize;
        file.read(reinterpret_cast<char*>(&optHeaderSize), 2);
        file.seekg(2 + optHeaderSize, std::ios::cur); // skip characteristics + optional header

        std::ostringstream oss;
        oss << "Sections (" << numSections << "):\n\n";
        oss << std::left << std::setw(10) << "Name" << std::setw(12) << "VirtSize"
            << std::setw(12) << "VirtAddr" << std::setw(12) << "RawSize" << "RawAddr\n";
        oss << std::string(56, '-') << "\n";

        for (int i = 0; i < numSections && i < 50; i++) {
            char name[9] = {};
            file.read(name, 8);
            uint32_t virtSize, virtAddr, rawSize, rawAddr;
            file.read(reinterpret_cast<char*>(&virtSize), 4);
            file.read(reinterpret_cast<char*>(&virtAddr), 4);
            file.read(reinterpret_cast<char*>(&rawSize), 4);
            file.read(reinterpret_cast<char*>(&rawAddr), 4);
            file.seekg(16, std::ios::cur); // skip relocations, line numbers, characteristics

            oss << std::left << std::setw(10) << name
                << "0x" << std::hex << std::setw(10) << virtSize
                << "0x" << std::setw(10) << virtAddr
                << "0x" << std::setw(10) << rawSize
                << "0x" << rawAddr << "\n";
        }

        return ToolResult::ok(oss.str());
    }

    ToolResult disassemble(const std::string& path, int offset, int length) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open file");

        // Auto-detect entry point if offset == -1
        uint64_t baseAddr = 0;
        if (offset < 0) {
            auto ep = getEntryPoint(path);
            offset = ep.fileOffset;
            baseAddr = ep.virtualAddr;
        }

        file.seekg(offset);
        std::vector<uint8_t> code(length);
        file.read(reinterpret_cast<char*>(code.data()), length);
        int bytesRead = (int)file.gcount();
        if (bytesRead == 0) return ToolResult::fail("No data at offset " + std::to_string(offset));

        // Auto-detect architecture from PE/ELF header
        cs_arch arch = CS_ARCH_X86;
        cs_mode mode = CS_MODE_64;
        std::ifstream hdrFile(path, std::ios::binary);
        if (hdrFile) {
            uint8_t magic[2];
            hdrFile.read(reinterpret_cast<char*>(magic), 2);
            if (magic[0] == 'M' && magic[1] == 'Z') {
                // PE file: read machine type
                hdrFile.seekg(0x3C);
                uint32_t peOff = 0;
                hdrFile.read(reinterpret_cast<char*>(&peOff), 4);
                hdrFile.seekg(peOff + 4); // Skip PE signature
                uint16_t machine = 0;
                hdrFile.read(reinterpret_cast<char*>(&machine), 2);
                if (machine == 0xAA64) {
                    arch = CS_ARCH_ARM64; mode = CS_MODE_ARM;
                } else if (machine == 0x1C0 || machine == 0x1C4) {
                    arch = CS_ARCH_ARM; mode = CS_MODE_THUMB;
                } else if (machine == 0x14C) {
                    arch = CS_ARCH_X86; mode = CS_MODE_32;
                }
                // 0x8664 = x86-64, already the default
            } else if (magic[0] == 0x7F && magic[1] == 'E') {
                // ELF file: read e_machine
                hdrFile.seekg(4); // skip rest of EI_MAG
                uint8_t elfClass = 0;
                hdrFile.read(reinterpret_cast<char*>(&elfClass), 1);
                // e_machine is at offset 18 in ELF header
                hdrFile.seekg(18);
                uint16_t eMachine = 0;
                hdrFile.read(reinterpret_cast<char*>(&eMachine), 2);
                if (eMachine == 183) { // EM_AARCH64
                    arch = CS_ARCH_ARM64; mode = CS_MODE_ARM;
                } else if (eMachine == 40) { // EM_ARM
                    arch = CS_ARCH_ARM; mode = CS_MODE_THUMB;
                } else if (eMachine == 3) { // EM_386
                    arch = CS_ARCH_X86; mode = CS_MODE_32;
                } else if (eMachine == 62) { // EM_X86_64
                    arch = CS_ARCH_X86; mode = CS_MODE_64;
                }
            }
            hdrFile.close();
        }

        // Initialize Capstone
        csh handle;
        cs_insn* insn;
        if (cs_open(arch, mode, &handle) != CS_ERR_OK) {
            return ToolResult::fail("Failed to initialize disassembler");
        }
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

        size_t count = cs_disasm(handle, code.data(), bytesRead, baseAddr, 0, &insn);

        std::string archName = "x86-64";
        if (arch == CS_ARCH_ARM64) archName = "ARM64";
        else if (arch == CS_ARCH_ARM) archName = "ARM/Thumb";
        else if (arch == CS_ARCH_X86 && mode == CS_MODE_32) archName = "x86";

        std::ostringstream oss;
        oss << "Disassembly of " << path << " [" << archName << "] (offset 0x" << std::hex << offset
            << ", " << std::dec << bytesRead << " bytes, " << count << " instructions):\n\n";

        if (count > 0) {
            for (size_t i = 0; i < count && i < 100; i++) {
                // Address
                oss << "  0x" << std::hex << std::setw(8) << std::setfill('0') << insn[i].address << "  ";
                // Bytes
                for (int j = 0; j < insn[i].size && j < 8; j++) {
                    oss << std::hex << std::setw(2) << std::setfill('0') << (int)insn[i].bytes[j] << " ";
                }
                for (int j = insn[i].size; j < 8; j++) oss << "   ";
                // Mnemonic + operands
                oss << " " << insn[i].mnemonic << " " << insn[i].op_str << "\n";
            }
            if (count > 100) {
                oss << "\n  ... (" << (count - 100) << " more instructions)\n";
            }
            cs_free(insn, count);
        } else {
            oss << "  (no valid instructions found)\n";
        }

        cs_close(&handle);
        return ToolResult::ok(oss.str());
    }

    ToolResult findFunctions(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open file");

        // Read .text section
        auto textSection = getTextSection(path);
        if (textSection.data.empty()) {
            return ToolResult::fail("Cannot find .text section");
        }

        // Disassemble and find function prologues (push rbp; mov rbp, rsp)
        csh handle;
        cs_insn* insn;
        if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK) {
            return ToolResult::fail("Failed to initialize disassembler");
        }
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

        size_t count = cs_disasm(handle, textSection.data.data(), textSection.data.size(),
                                  textSection.virtualAddr, 0, &insn);

        std::ostringstream oss;
        oss << "Functions found in " << path << ":\n\n";

        int funcCount = 0;
        for (size_t i = 0; i < count && funcCount < 100; i++) {
            // Common function prologues
            bool isPrologue = false;
            if (strcmp(insn[i].mnemonic, "push") == 0 &&
                strstr(insn[i].op_str, "rbp")) {
                isPrologue = true;
            }
            // Also detect: sub rsp, N (another common prologue)
            if (strcmp(insn[i].mnemonic, "sub") == 0 &&
                strstr(insn[i].op_str, "rsp")) {
                if (i > 0 && strcmp(insn[i-1].mnemonic, "push") == 0) {
                    // Already counted above
                } else {
                    isPrologue = true;
                }
            }

            if (isPrologue) {
                oss << "  func_" << std::hex << insn[i].address
                    << " @ 0x" << insn[i].address << "\n";
                funcCount++;
            }
        }

        if (count > 0) cs_free(insn, count);
        cs_close(&handle);

        oss << "\n(" << funcCount << " functions detected by prologue pattern)";
        return ToolResult::ok(oss.str());
    }

    struct EntryPointInfo {
        int fileOffset = 0;
        uint64_t virtualAddr = 0;
    };

    EntryPointInfo getEntryPoint(const std::string& path) {
        EntryPointInfo ep;
        std::ifstream file(path, std::ios::binary);
        if (!file) return ep;

        uint8_t magic[2];
        file.read(reinterpret_cast<char*>(magic), 2);
        if (magic[0] != 'M' || magic[1] != 'Z') return ep;

        // PE entry point
        file.seekg(0x3C);
        uint32_t peOffset = 0;
        file.read(reinterpret_cast<char*>(&peOffset), 4);
        file.seekg(peOffset + 4 + 16); // Skip PE sig + COFF header (minus optional header size field)
        uint16_t optHeaderSize;
        file.read(reinterpret_cast<char*>(&optHeaderSize), 2);
        file.seekg(2, std::ios::cur); // Skip characteristics

        // Optional header: entry point at offset 16
        uint16_t optMagic;
        file.read(reinterpret_cast<char*>(&optMagic), 2);
        file.seekg(14, std::ios::cur); // Skip to AddressOfEntryPoint
        uint32_t entryRVA;
        file.read(reinterpret_cast<char*>(&entryRVA), 4);

        // Get ImageBase
        file.seekg(peOffset + 4 + 20 + 24); // COFF + optional header offset to ImageBase
        uint64_t imageBase = 0;
        if (optMagic == 0x20b) { // PE32+
            file.read(reinterpret_cast<char*>(&imageBase), 8);
        } else {
            uint32_t base32;
            file.read(reinterpret_cast<char*>(&base32), 4);
            imageBase = base32;
        }

        ep.virtualAddr = imageBase + entryRVA;
        // Approximate file offset (assumes .text is first section at typical offset)
        ep.fileOffset = entryRVA; // Simplified — real impl would map RVA to file offset
        return ep;
    }

    struct SectionData {
        std::vector<uint8_t> data;
        uint64_t virtualAddr = 0;
    };

    SectionData getTextSection(const std::string& path) {
        SectionData result;
        std::ifstream file(path, std::ios::binary);
        if (!file) return result;

        uint8_t magic[2];
        file.read(reinterpret_cast<char*>(magic), 2);
        if (magic[0] != 'M' || magic[1] != 'Z') return result;

        file.seekg(0x3C);
        uint32_t peOffset = 0;
        file.read(reinterpret_cast<char*>(&peOffset), 4);

        file.seekg(peOffset + 4);
        uint16_t machine, numSections;
        file.read(reinterpret_cast<char*>(&machine), 2);
        file.read(reinterpret_cast<char*>(&numSections), 2);
        file.seekg(12, std::ios::cur); // Skip timestamp, symbol table, num symbols
        uint16_t optHeaderSize;
        file.read(reinterpret_cast<char*>(&optHeaderSize), 2);
        file.seekg(2 + optHeaderSize, std::ios::cur); // Skip characteristics + optional header

        // Find .text section
        for (int i = 0; i < numSections; i++) {
            char name[9] = {};
            file.read(name, 8);
            uint32_t virtSize, virtAddr, rawSize, rawAddr;
            file.read(reinterpret_cast<char*>(&virtSize), 4);
            file.read(reinterpret_cast<char*>(&virtAddr), 4);
            file.read(reinterpret_cast<char*>(&rawSize), 4);
            file.read(reinterpret_cast<char*>(&rawAddr), 4);
            file.seekg(16, std::ios::cur);

            if (std::string(name) == ".text") {
                int readSize = std::min(rawSize, (uint32_t)(1024 * 1024)); // Cap at 1MB
                result.data.resize(readSize);
                file.seekg(rawAddr);
                file.read(reinterpret_cast<char*>(result.data.data()), readSize);
                result.virtualAddr = virtAddr;
                return result;
            }
        }
        return result;
    }

    // === CUDA-accelerated analysis methods ===

    ToolResult analyzeEntropy(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open file");
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});

        auto& analyzer = CudaBinaryAnalyzer::getInstance();
        auto blocks = analyzer.calculateEntropy(data, 4096);

        std::ostringstream oss;
        oss << "Entropy analysis of " << path << " (" << data.size() << " bytes, "
            << blocks.size() << " blocks):\n";
        oss << (analyzer.isGpuAvailable() ? "[CUDA accelerated]\n\n" : "[CPU mode]\n\n");

        // Summary
        int code = 0, dataB = 0, compressed = 0, encrypted = 0, padding = 0;
        for (const auto& b : blocks) {
            if (b.classification == "code") code++;
            else if (b.classification == "data") dataB++;
            else if (b.classification == "compressed") compressed++;
            else if (b.classification == "encrypted") encrypted++;
            else if (b.classification == "padding") padding++;
        }
        oss << "Summary:\n";
        oss << "  Code blocks:       " << code << " (" << (blocks.empty() ? 0 : code*100/blocks.size()) << "%)\n";
        oss << "  Data blocks:       " << dataB << " (" << (blocks.empty() ? 0 : dataB*100/blocks.size()) << "%)\n";
        oss << "  Compressed blocks: " << compressed << "\n";
        oss << "  Encrypted blocks:  " << encrypted << "\n";
        oss << "  Padding blocks:    " << padding << "\n\n";

        // Show first 20 blocks detail
        oss << "Block details (first 20):\n";
        oss << std::left << std::setw(12) << "Offset" << std::setw(10) << "Entropy" << "Type\n";
        oss << std::string(35, '-') << "\n";
        for (size_t i = 0; i < std::min(blocks.size(), (size_t)20); i++) {
            oss << "0x" << std::hex << std::setw(10) << std::setfill('0') << blocks[i].offset
                << std::dec << std::setfill(' ') << std::setw(10) << std::fixed << std::setprecision(2) << blocks[i].entropy
                << blocks[i].classification << "\n";
        }

        return ToolResult::ok(oss.str());
    }

    ToolResult detectCrypto(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open file");
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});

        auto& analyzer = CudaBinaryAnalyzer::getInstance();
        auto sigs = analyzer.detectCrypto(data);

        std::ostringstream oss;
        oss << "Crypto/packer detection for " << path << ":\n";
        oss << (analyzer.isGpuAvailable() ? "[CUDA accelerated]\n\n" : "[CPU mode]\n\n");

        if (sigs.empty()) {
            oss << "No known crypto signatures or packers detected.\n";
        } else {
            for (const auto& sig : sigs) {
                oss << "  [" << std::fixed << std::setprecision(0) << (sig.confidence * 100) << "%] "
                    << sig.name;
                if (sig.offset > 0) oss << " @ offset 0x" << std::hex << sig.offset;
                oss << "\n";
            }
        }
        return ToolResult::ok(oss.str());
    }

    ToolResult compareBinaries(const std::string& path1, const std::string& path2) {
        std::ifstream f1(path1, std::ios::binary), f2(path2, std::ios::binary);
        if (!f1) return ToolResult::fail("Cannot open: " + path1);
        if (!f2) return ToolResult::fail("Cannot open: " + path2);

        std::vector<uint8_t> data1((std::istreambuf_iterator<char>(f1)), {});
        std::vector<uint8_t> data2((std::istreambuf_iterator<char>(f2)), {});

        auto& analyzer = CudaBinaryAnalyzer::getInstance();
        auto result = analyzer.compareBinaries(data1, data2);

        std::ostringstream oss;
        oss << "Binary comparison:\n";
        oss << (analyzer.isGpuAvailable() ? "[CUDA accelerated]\n\n" : "[CPU mode]\n\n");
        oss << "  File 1: " << path1 << " (" << data1.size() << " bytes)\n";
        oss << "  File 2: " << path2 << " (" << data2.size() << " bytes)\n\n";
        oss << "  Overall similarity: " << std::fixed << std::setprecision(1) << (result.overallSimilarity * 100) << "%\n";
        oss << "  Code similarity:    " << (result.codeSimilarity * 100) << "%\n";
        oss << "  Data similarity:    " << (result.dataSimilarity * 100) << "%\n";

        if (result.overallSimilarity > 0.9f) oss << "\n  Verdict: Nearly identical binaries\n";
        else if (result.overallSimilarity > 0.7f) oss << "\n  Verdict: Same codebase, different build/version\n";
        else if (result.overallSimilarity > 0.4f) oss << "\n  Verdict: Some shared code/libraries\n";
        else oss << "\n  Verdict: Unrelated binaries\n";

        return ToolResult::ok(oss.str());
    }
};

} // namespace closecrab
