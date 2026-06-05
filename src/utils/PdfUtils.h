#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <fstream>

namespace closecrab {

struct PdfPageRange {
    int firstPage;  // 1-indexed
    int lastPage;   // can be INT_MAX for "to end"
};

// Parse page range strings like "1-5", "3", "10-20"
inline std::optional<PdfPageRange> parsePdfPageRange(const std::string& range) {
  if (range.empty()) return std::nullopt;

    auto pos = range.find('-');
    if (pos == std::string::npos) {
      // Single page: "3"
        try {
            int page = std::stoi(range);
            if (page < 1) return std::nullopt;
            return PdfPageRange{page, page};
        } catch (...) {
            return std::nullopt;
      }
    }

    // Range: "1-5" or "10-" (to end)
    try {
      std::string first = range.substr(0, pos);
        std::string last = range.substr(pos + 1);

        int firstPage = std::stoi(first);
        if (firstPage < 1) return std::nullopt;

        int lastPage = last.empty() ? INT_MAX : std::stoi(last);
    if (lastPage < firstPage) return std::nullopt;

        return PdfPageRange{firstPage, lastPage};
    } catch (...) {
        return std::nullopt;
    }
}

// Check if PDF tools are available (pdftotext, pdftoppm from poppler-utils)
inline bool isPdfToolsAvailable() {
#ifdef _WIN32
    // Try to find pdftotext.exe in PATH or common locations
    return std::filesystem::exists("pdftotext.exe") ||
           std::filesystem::exists("C:\\Program Files\\poppler\\bin\\pdftotext.exe");
#else
    // Unix: check if pdftotext is in PATH
    return system("which pdftotext > /dev/null 2>&1") == 0;
#endif
}

// Get PDF page count using pdfinfo
inline int getPdfPageCount(const std::filesystem::path& pdfPath) {
    if (!std::filesystem::exists(pdfPath)) return -1;

#ifdef _WIN32
    std::string cmd = "pdfinfo \"" + pdfPath.string() + "\" 2>nul";
#else
    std::string cmd = "pdfinfo '" + pdfPath.string() + "' 2>/dev/null";
#endif

    FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return -1;

    char buffer[256];
    int pageCount = -1;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        if (line.find("Pages:") == 0) {
            try {
              pageCount = std::stoi(line.substr(6));
          } catch (...) {}
            break;
        }
    }
    pclose(pipe);
    return pageCount;
}

// Extract PDF text using pdftotext
inline std::string extractPdfText(const std::filesystem::path& pdfPath,
                                 const std::optional<PdfPageRange>& range = std::nullopt) {
    if (!std::filesystem::exists(pdfPath)) {
        return "[Error: PDF file not found]";
    }

    if (!isPdfToolsAvailable()) {
        return "[Error: PDF tools not installed. Install poppler-utils:\n"
               "  Windows: choco install poppler\n"
               "  macOS: brew install poppler\n"
         "  Linux: apt-get install poppler-utils]";
    }

    // Create temporary output file
    std::string tempFile = std::filesystem::temp_directory_path().string() + "/pdf_text_" +
                std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".txt";

    std::string cmd;
    if (range.has_value()) {
        int last = range->lastPage == INT_MAX ? -1 : range->lastPage;
        cmd = "pdftotext -f " + std::to_string(range->firstPage) +
              " -l " + (last == -1 ? "" : std::to_string(last)) +
           " \"" + pdfPath.string() + "\" \"" + tempFile + "\"";
    } else {
        cmd = "pdftotext \"" + pdfPath.string() + "\" \"" + tempFile + "\"";
    }

    int ret = system(cmd.c_str());
    if (ret != 0) {
        return "[Error: Failed to extract PDF text (exit code " + std::to_string(ret) + ")]";
    }

    // Read extracted text
    std::ifstream file(tempFile);
    if (!file) {
        std::filesystem::remove(tempFile);
     return "[Error: Could not read extracted text]";
    }
    std::string content((std::istreambuf_iterator<char>(file)), {});
    file.close();
    std::filesystem::remove(tempFile);

    return content;
}

} // namespace closecrab
