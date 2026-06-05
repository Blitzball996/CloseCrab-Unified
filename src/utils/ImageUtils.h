#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdint>

namespace closecrab {

// Detect image format from buffer (magic bytes)
inline std::string detectImageFormat(const std::vector<uint8_t>& data) {
    if (data.size() < 8) return "unknown";

    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 0x50 &&
        data[2] == 0x4E && data[3] == 0x47) {
        return "png";
    }

    // JPEG: FF D8 FF
    if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return "jpeg";
    }

    // GIF: 47 49 46 38 (GIF8)
  if (data.size() >= 4 && data[0] == 0x47 && data[1] == 0x49 &&
        data[2] == 0x46 && data[3] == 0x38) {
        return "gif";
    }

    // WebP: 52 49 46 46 ... 57 45 42 50 (RIFF...WEBP)
    if (data.size() >= 12 && data[0] == 0x52 && data[1] == 0x49 &&
        data[2] == 0x46 && data[3] == 0x46 && data[8] == 0x57 &&
        data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
        return "webp";
    }

    // BMP: 42 4D (BM)
    if (data.size() >= 2 && data[0] == 0x42 && data[1] == 0x4D) {
        return "bmp";
    }

    return "unknown";
}

// Base64 encode
inline std::string base64Encode(const std::vector<uint8_t>& data) {
    static const char* chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((data.size() + 2) / 3 * 4);

    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);

        result += chars[(n >> 18) & 0x3F];
        result += chars[(n >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < data.size()) ? chars[n & 0x3F] : '=';
    }
    return result;
}

// Read image file and encode to base64
inline std::string readImageAsBase64(const std::filesystem::path& imagePath,
                                  std::string& outMediaType,
             size_t& outOriginalSize) {
    if (!std::filesystem::exists(imagePath)) {
        return "";
    }

    // Read file
    std::ifstream file(imagePath, std::ios::binary);
    if (!file) return "";

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    outOriginalSize = data.size();

    // Detect format
    std::string format = detectImageFormat(data);
    if (format == "unknown") {
        // Fallback to extension
        std::string ext = imagePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".png") format = "png";
        else if (ext == ".jpg" || ext == ".jpeg") format = "jpeg";
        else if (ext == ".gif") format = "gif";
        else if (ext == ".webp") format = "webp";
        else if (ext == ".bmp") format = "bmp";
    }

    outMediaType = "image/" + format;
    return base64Encode(data);
}

// Estimate token count for base64 image
// Anthropic: ~1.2 tokens per base64 char for images
inline size_t estimateImageTokens(size_t base64Length) {
    return static_cast<size_t>(base64Length * 1.2);
}

// Check if image exceeds token budget
inline bool imageExceedsTokenBudget(size_t fileSize, size_t maxTokens) {
    // base64 is ~1.33x original size, then ~1.2 tokens per char
    size_t estimatedTokens = static_cast<size_t>(fileSize * 1.33 * 1.2);
    return estimatedTokens > maxTokens;
}

} // namespace closecrab
