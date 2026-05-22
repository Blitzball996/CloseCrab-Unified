#pragma once
#include "../Tool.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>

namespace closecrab {

class ImageInputTool : public Tool {
public:
    std::string getName() const override { return "ImageInput"; }
    std::string getDescription() const override {
        return "Send an image to the AI for analysis. Supports PNG, JPG, BMP, GIF, WebP. "
               "Use for screenshot analysis, UI review, diagram understanding.";
    }
    std::string getCategory() const override { return "input"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"file_path", {{"type", "string"}, {"description", "Path to image file (png/jpg/bmp/gif/webp)"}}},
                {"question", {{"type", "string"}, {"description", "Question about the image (optional)"}}}
            }},
            {"required", {"file_path"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        namespace fs = std::filesystem;
        std::string path = input["file_path"].get<std::string>();
        std::string question = input.value("question", "Describe this image.");

        if (!fs::exists(path)) return ToolResult::fail("Image not found: " + path);

        // Detect media type from extension
        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        std::string mediaType;
        if (ext == ".png") mediaType = "image/png";
        else if (ext == ".jpg" || ext == ".jpeg") mediaType = "image/jpeg";
        else if (ext == ".gif") mediaType = "image/gif";
        else if (ext == ".bmp") mediaType = "image/bmp";
        else if (ext == ".webp") mediaType = "image/webp";
        else return ToolResult::fail("Unsupported image format: " + ext);

        // Check file size (max 20MB for Anthropic API)
        auto fileSize = fs::file_size(path);
        if (fileSize > 20 * 1024 * 1024) {
            return ToolResult::fail("Image too large: " + std::to_string(fileSize / 1024 / 1024) + "MB (max 20MB)");
        }

        // Read and base64 encode
        std::ifstream file(path, std::ios::binary);
        if (!file) return ToolResult::fail("Cannot open image: " + path);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
        std::string base64 = base64Encode(data);

        // Build result with image data for the next API call
        ToolResult result;
        result.success = true;
        result.content = "Image loaded: " + path + " (" + std::to_string(fileSize / 1024) + "KB, " + mediaType + ")";
        result.data = {
            {"type", "image"},
            {"media_type", mediaType},
            {"base64", base64},
            {"file_path", path},
            {"size_bytes", fileSize},
            {"question", question}
        };
        result.hasContextModification = true;
        result.contextModification = {
            {"action", "add_image"},
            {"media_type", mediaType},
            {"data", base64}
        };
        return result;
    }

private:
    static std::string base64Encode(const std::vector<uint8_t>& data) {
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
};

} // namespace closecrab
