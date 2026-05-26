#pragma once

#include <string>
#include <fstream>
#include <filesystem>

namespace closecrab {

class OutputPersistence {
public:
    static constexpr size_t MAX_INLINE_SIZE = 50000;  // 50,000 chars (same as JackProAi DEFAULT_MAX_RESULT_SIZE_CHARS)

    /// If output exceeds MAX_INLINE_SIZE, save to disk and return a preview.
    /// On failure, returns the original output unchanged.
    static std::string persistIfNeeded(const std::string& output,
                                       const std::string& toolName,
                                       const std::string& toolUseId) {
        if (output.size() <= MAX_INLINE_SIZE) {
            return output;
        }

        namespace fs = std::filesystem;
        try {
            fs::path dir = "data/tool-results";
            fs::create_directories(dir);

            fs::path filePath = dir / (toolUseId + ".txt");
            {
                std::ofstream ofs(filePath, std::ios::binary);
                if (!ofs) return output;
                ofs.write(output.data(), static_cast<std::streamsize>(output.size()));
            }

            // Build preview: first 2000 bytes (same as JackProAi PREVIEW_SIZE_BYTES)
            std::string preview = output.substr(0, 2000);

            return preview
                + "\n\n[Full output saved to: " + filePath.string()
                + "] (" + std::to_string(output.size()) + " bytes)"
                + "\nUse Read tool to view specific sections.";
        } catch (...) {
            return output;
        }
    }
};

} // namespace closecrab
