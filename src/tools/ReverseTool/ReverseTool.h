#pragma once
#include "../Tool.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <vector>

namespace closecrab {

class ReverseTool : public Tool {
public:
    std::string getName() const override { return "Reverse"; }
    std::string getDescription() const override {
        return "Binary analysis: hex dump, string extraction, PE/ELF header parsing. "
               "Use for reverse engineering and binary file inspection.";
    }
    std::string getCategory() const override { return "analysis"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"file_path", {{"type", "string"}, {"description", "Path to binary file"}}},
                {"action", {{"type", "string"}, {"enum", {"hexdump", "strings", "headers", "imports", "sections"}},
                    {"description", "Analysis action"}}},
                {"offset", {{"type", "integer"}, {"description", "Start offset for hexdump (default 0)"}}},
                {"length", {{"type", "integer"}, {"description", "Bytes to read for hexdump (default 256)"}}},
                {"min_length", {{"type", "integer"}, {"description", "Minimum string length for strings action (default 4)"}}}
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
};

} // namespace closecrab
