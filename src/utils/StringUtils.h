#pragma once

#include <string>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace closecrab {

// Hash the raw on-disk bytes of a file. Used by the read-before-write staleness
// check (JackProAi FileWriteTool.ts:282-294 content fallback): when mtime
// changed but the content hash matches, the write is allowed (mtime false
// positive from cloud sync / antivirus on Windows). Returns {hash, size};
// hash==0 && size==0 on failure.
inline std::pair<size_t, uint64_t> fileContentHash(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {0, 0};
    std::string data((std::istreambuf_iterator<char>(f)), {});
    return { std::hash<std::string>{}(data), (uint64_t)data.size() };
}

// Build a std::filesystem::path from a UTF-8 string without going through the
// ANSI code page. On Windows, constructing fs::path from a narrow std::string
// interprets the bytes as the active code page (936/GBK here), which throws on
// CJK paths like "需求.txt". fs::u8path interprets the bytes as UTF-8 and
// produces the correct native (wide) path. Use this for ALL file I/O paths that
// originate from tool input (which arrives as UTF-8 JSON).
inline std::filesystem::path utf8Path(const std::string& p) {
    try {
        return std::filesystem::u8path(p);
    } catch (...) {
        return std::filesystem::path(p);
    }
}

// Normalize a path key for readFileState lookups so the same file resolves to
// the same key regardless of forward/back slashes or case (Windows).
// Mirrors JackProAi fileStateCache.ts path normalization (normalize() before
// access). Without this, a Read of "G:/foo/x" and a Write of "G:\foo\x" would
// miss each other in the map.
inline std::string normalizePathKey(const std::string& p) {
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        out.push_back(c == '\\' ? '/' : c);
    }
#ifdef _WIN32
    // Windows filesystem is case-insensitive — lowercase for stable keys.
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char ch) { return (char)std::tolower(ch); });
#endif
    return out;
}

// Sanitize a string to valid UTF-8 by replacing invalid bytes with '?'
// This prevents nlohmann::json from throwing on non-UTF-8 content
inline std::string sanitizeUtf8(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (c < 0x80) {
            // ASCII
            result += input[i];
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 < input.size() && (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80) {
                result += input[i];
                result += input[i+1];
                i += 2;
            } else {
                result += '?';
                i++;
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 < input.size() &&
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80) {
                result += input[i];
                result += input[i+1];
                result += input[i+2];
                i += 3;
            } else {
                result += '?';
                i++;
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte sequence
            if (i + 3 < input.size() &&
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+3]) & 0xC0) == 0x80) {
                result += input[i];
                result += input[i+1];
                result += input[i+2];
                result += input[i+3];
                i += 4;
            } else {
                result += '?';
                i++;
            }
        } else {
            // Invalid leading byte
            result += '?';
            i++;
        }
    }
    return result;
}

#ifdef _WIN32
// Convert GBK/ANSI string to UTF-8 (Windows only)
inline std::string gbkToUtf8(const std::string& gbk) {
    if (gbk.empty()) return gbk;

    // GBK -> UTF-16
    int wlen = MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), (int)gbk.size(), nullptr, 0);
    if (wlen <= 0) return sanitizeUtf8(gbk);

    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, gbk.c_str(), (int)gbk.size(), &wstr[0], wlen);

    // UTF-16 -> UTF-8
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return sanitizeUtf8(gbk);

    std::string utf8(ulen, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wlen, &utf8[0], ulen, nullptr, nullptr);
    return utf8;
}
#endif

// Ensure a string is valid UTF-8, converting from GBK on Windows if needed
inline std::string ensureUtf8(const std::string& input) {
    if (input.empty()) return input;

    // Quick check: is it already valid UTF-8?
    bool valid = true;
    for (size_t i = 0; i < input.size() && valid; ) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c < 0x80) { i++; continue; }

        int expected = 0;
        if ((c & 0xE0) == 0xC0) expected = 1;
        else if ((c & 0xF0) == 0xE0) expected = 2;
        else if ((c & 0xF8) == 0xF0) expected = 3;
        else { valid = false; break; }

        for (int j = 0; j < expected; j++) {
            if (i + 1 + j >= input.size() || (static_cast<unsigned char>(input[i+1+j]) & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        i += 1 + expected;
    }

    if (valid) return input;

#ifdef _WIN32
    // Try GBK -> UTF-8 conversion
    return gbkToUtf8(input);
#else
    return sanitizeUtf8(input);
#endif
}

} // namespace closecrab
