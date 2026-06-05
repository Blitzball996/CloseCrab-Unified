#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdint>

// stb_image: bundled single-header image lib (zero external downloads).
// The implementation lives in ImageUtils.cpp; stb wraps its public API in
// extern "C", so these forward declarations MUST use C linkage to resolve.
// Only the functions actually used are declared; the resize entry point takes
// an enum in real stb and is intentionally not declared here (resize is a no-op
// in the header-only path).
#ifdef CLOSECRAB_HAS_STB_IMAGE
#ifndef STB_IMAGE_DECLS_INCLUDED
#define STB_IMAGE_DECLS_INCLUDED
extern "C" {
    unsigned char* stbi_load_from_memory(unsigned char const* buffer, int len,
                                         int* x, int* y, int* channels_in_file,
                                         int desired_channels);
    void stbi_image_free(void* retval_from_stbi_load);
    int stbi_write_jpg_to_func(void (*func)(void* context, void* data, int size),
                               void* context, int w, int h, int comp,
                               const void* data, int quality);
}
#endif
#endif

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

struct ImageDimensions {
    int originalWidth = 0;
    int originalHeight = 0;
    int displayWidth = 0;
    int displayHeight = 0;
};

// Callback for stbi_write_jpg_to_func (captures output to vector)
inline void jpegWriteCallback(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    uint8_t* bytes = static_cast<uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

// Compress image with token budget enforcement (JackProAi alignment)
// Implements: resize to max 1024x1024, progressive JPEG quality reduction.
// Requires the bundled stb implementation (ImageUtils.cpp). When stb is not
// compiled in, callers fall back to sending the original bytes.
#ifdef CLOSECRAB_HAS_STB_IMAGE
inline std::vector<uint8_t> compressImageWithTokenBudget(
    const std::vector<uint8_t>& inputData,
    size_t maxTokens,
    ImageDimensions* outDimensions = nullptr) {

    // Load image
    int width, height, channels;
    unsigned char* img = stbi_load_from_memory(
        inputData.data(), static_cast<int>(inputData.size()),
      &width, &height, &channels, 3); // Force RGB

    if (!img) {
     // Fallback: return original if decode fails
        return inputData;
    }

    if (outDimensions) {
        outDimensions->originalWidth = width;
        outDimensions->originalHeight = height;
    }

    // Resize if larger than 1024x1024 (JackProAi behavior)
    constexpr int MAX_DIM = 1024;
    int targetWidth = width;
    int targetHeight = height;
    std::vector<uint8_t> resizedData;

    if (width > MAX_DIM || height > MAX_DIM) {
      // Maintain aspect ratio
        float scale = std::min(static_cast<float>(MAX_DIM) / width,
                     static_cast<float>(MAX_DIM) / height);
        targetWidth = static_cast<int>(width * scale);
        targetHeight = static_cast<int>(height * scale);

        resizedData.resize(targetWidth * targetHeight * 3);

        // Use stb_image_resize (linear interpolation)
        // Note: stbir_resize_uint8_linear is from stb_image_resize.h
        // For now, we'll use a simple implementation or link against it
        // Simplified: we'll just use the original for POC
        // TODO: Integrate stb_image_resize.h properly

        // For now, skip resize in header-only version
        targetWidth = width;
        targetHeight = height;
    }

    if (outDimensions) {
        outDimensions->displayWidth = targetWidth;
        outDimensions->displayHeight = targetHeight;
    }

    // Progressive quality reduction to meet token budget
    std::vector<uint8_t> output;
    size_t targetBytes = (maxTokens / 1.2) / 1.33; // Reverse token calculation

    for (int quality = 85; quality >= 20; quality -= 15) {
        output.clear();
        stbi_write_jpg_to_func(jpegWriteCallback, &output,
                      targetWidth, targetHeight, 3, img, quality);

        if (output.size() <= targetBytes) {
          break; // Found acceptable quality
        }
    }

    stbi_image_free(img);
    return output;
}
#endif // CLOSECRAB_HAS_STB_IMAGE

// Read image file and encode to base64 with optional compression
inline std::string readImageAsBase64(const std::filesystem::path& imagePath,
                             std::string& outMediaType,
                               size_t& outOriginalSize,
                           size_t maxTokens = 0,
                   ImageDimensions* outDimensions = nullptr) {
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

    // Apply compression if token budget specified
    if (maxTokens > 0 && imageExceedsTokenBudget(data.size(), maxTokens)) {
#ifdef CLOSECRAB_HAS_STB_IMAGE
        data = compressImageWithTokenBudget(data, maxTokens, outDimensions);
        format = "jpeg"; // Compression always produces JPEG
#endif
        // Without stb, fall through and return the original bytes; the caller's
        // token-budget guard already rejects images that are too large.
    }

    outMediaType = "image/" + format;
    return base64Encode(data);
}

} // namespace closecrab
