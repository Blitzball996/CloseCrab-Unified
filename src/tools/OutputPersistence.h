#pragma once

#include <string>
#include <fstream>
#include <filesystem>

namespace closecrab {

class OutputPersistence {
public:
    static constexpr size_t MAX_INLINE_SIZE = 5000;  // ~1250 tokens — proxy safe (JackProAi uses 50K but our proxy is stricter)

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

            // Build preview: first 500 + ... + last 500
            std::string preview = output.substr(0, 500)
                + "\n...\n"
                + output.substr(output.size() - 500);

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
