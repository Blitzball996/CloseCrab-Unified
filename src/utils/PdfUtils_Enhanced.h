#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <climits>
#include <cstdio>

// Conditional poppler integration. CMake defines CLOSECRAB_HAS_POPPLER when the
// poppler-cpp library is found; accept the legacy HAS_POPPLER spelling too.
#if defined(CLOSECRAB_HAS_POPPLER) && !defined(HAS_POPPLER)
#define HAS_POPPLER
#endif

#ifdef HAS_POPPLER
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>
#endif

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

#ifdef HAS_POPPLER

// Get PDF page count using poppler library
inline int getPdfPageCount(const std::filesystem::path& pdfPath) {
    try {
        auto doc = poppler::document::load_from_file(pdfPath.string());
        if (!doc) return -1;
        return doc->pages();
  } catch (...) {
        return -1;
    }
}

// Extract PDF text using poppler library (native C++)
inline std::string extractPdfText(const std::filesystem::path& pdfPath,
                       const std::optional<PdfPageRange>& range = std::nullopt) {
    try {
        auto doc = poppler::document::load_from_file(pdfPath.string());
     if (!doc) {
            return "[Error: Cannot open PDF file]";
        }

        if (doc->is_locked()) {
            return "[Error: PDF is password-protected]";
        }

        int pageCount = doc->pages();
        int startPage = range.has_value() ? range->firstPage : 1;
        int endPage = range.has_value() ?
          (range->lastPage == INT_MAX ? pageCount : range->lastPage) : pageCount;

        // Clamp to actual page count
        startPage = std::max(1, std::min(startPage, pageCount));
        endPage = std::max(startPage, std::min(endPage, pageCount));

        std::string result;

        for (int i = startPage - 1; i < endPage; i++) { // poppler is 0-indexed
          auto page = doc->create_page(i);
          if (!page) continue;

            // Extract text with layout preserved
            poppler::page::text_layout_enum layout = poppler::page::physical_layout;
          std::string pageText = page->text(poppler::rectf(), layout).to_utf8();

            result += "=== Page " + std::to_string(i + 1) + " ===\n";
         result += pageText;
         result += "\n\n";
      }

        return result;
    } catch (const std::exception& e) {
        return "[Error: " + std::string(e.what()) + "]";
    }
}

#else // Fallback to external pdftotext

// MSVC spells the POSIX pipe API with a leading underscore.
#ifdef _WIN32
#define CLOSECRAB_POPEN  _popen
#define CLOSECRAB_PCLOSE _pclose
#else
#define CLOSECRAB_POPEN  popen
#define CLOSECRAB_PCLOSE pclose
#endif

// Check if PDF tools are available (pdftotext from poppler-utils)
inline bool isPdfToolsAvailable() {
#ifdef _WIN32
    // Try to find pdftotext.exe in PATH or common locations
    return std::filesystem::exists("pdftotext.exe") ||
           std::filesystem::exists("C:\\Program Files\\poppler\\bin\\pdftotext.exe") ||
        std::filesystem::exists("C:\\msys64\\mingw64\\bin\\pdftotext.exe");
#else
    // Unix: check if pdftotext is in PATH
    return system("which pdftotext > /dev/null 2>&1") == 0;
#endif
}

// Get PDF page count using pdfinfo (external tool)
inline int getPdfPageCount(const std::filesystem::path& pdfPath) {
    if (!std::filesystem::exists(pdfPath)) return -1;

#ifdef _WIN32
    std::string cmd = "pdfinfo \"" + pdfPath.string() + "\" 2>nul";
#else
    std::string cmd = "pdfinfo '" + pdfPath.string() + "' 2>/dev/null";
#endif

    FILE* pipe = CLOSECRAB_POPEN(cmd.c_str(), "r");
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
    CLOSECRAB_PCLOSE(pipe);
    return pageCount;
}

// Extract PDF text using pdftotext (external tool)
inline std::string extractPdfText(const std::filesystem::path& pdfPath,
                  const std::optional<PdfPageRange>& range = std::nullopt) {
    if (!std::filesystem::exists(pdfPath)) {
        return "[Error: PDF file not found]";
    }

    if (!isPdfToolsAvailable()) {
        return "[Error: PDF tools not installed. Install poppler-utils:\n"
               "  Windows: choco install poppler\n"
            "  macOS: brew install poppler\n"
            "  Linux: apt-get install poppler-utils\n"
          "\nAlternatively, rebuild CloseCrab with HAS_POPPLER to use native PDF support.]";
    }

    // Create temporary output file
    namespace fs = std::filesystem;
    std::string tempFile = (fs::temp_directory_path() / ("pdf_text_" +
          std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
         ".txt")).string();

    std::ostringstream cmdBuilder;
    cmdBuilder << "pdftotext ";

    if (range.has_value()) {
        cmdBuilder << "-f " << range->firstPage << " ";
     if (range->lastPage != INT_MAX) {
      cmdBuilder << "-l " << range->lastPage << " ";
        }
    }

    cmdBuilder << "\"" << pdfPath.string() << "\" \"" << tempFile << "\"";

    int ret = system(cmdBuilder.str().c_str());
    if (ret != 0) {
        return "[Error: Failed to extract PDF text (exit code " + std::to_string(ret) + ")]";
    }

    // Read extracted text
    std::ifstream file(tempFile);
    if (!file) {
        fs::remove(tempFile);
        return "[Error: Could not read extracted text]";
    }
    std::string content((std::istreambuf_iterator<char>(file)), {});
    file.close();
    fs::remove(tempFile);

    return content;
}

#endif // HAS_POPPLER

// User-facing error message for unsupported PDF operations
inline std::string getPdfNotSupportedMessage() {
#ifdef HAS_POPPLER
    return "[Error: PDF processing failed]";
#else
    return "[Error: PDF support not available. Install poppler-utils or rebuild with HAS_POPPLER]";
#endif
}

} // namespace closecrab
