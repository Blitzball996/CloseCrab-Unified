#pragma once

#include "../Tool.h"
#include "../../core/FileStateCache.h"
#include "../../core/MmapCache.h"
#include "../../utils/StringUtils.h"
#include "../../utils/PdfUtils_Enhanced.h"
#include "../../utils/ImageUtils_Enhanced.h"
#include "../../utils/FileSecurityUtils.h"
#include "../../utils/NotebookUtils.h"
#include "../../core/PermissionManager.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <cstdlib>

namespace closecrab {

// Maximum pages per PDF read request (JackProAi constant)
constexpr int PDF_MAX_PAGES_PER_READ = 20;

// Maximum token budget for file reads (default)
constexpr size_t DEFAULT_MAX_TOKENS = 1000;

// Maximum characters rendered for a single line before it is truncated
// (claude-code parity: prevents a minified one-line file from flooding context).
constexpr size_t MAX_LINE_CHARS = 2000;

class FileReadToolEnhanced : public Tool {
public:
    std::string getName() const override { return "Read"; }
    std::string getDescription() const override {
        return "Read file contents. Supports text files, images (PNG/JPG/GIF/WebP), and PDFs. "
           "Use offset/limit for large text files, pages parameter for PDFs.";
    }
    std::string getCategory() const override { return "file"; }
    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
           {"file_path", {{"type", "string"}, {"description", "Absolute path to the file"}}},
                {"offset", {{"type", "integer"}, {"description", "Line number to start from (0-based)"}}},
           {"limit", {{"type", "integer"}, {"description", "Max lines to read"}}},
            {"pages", {{"type", "string"}, {"description", "Page range for PDF files (e.g., \"1-5\", \"3\", \"10-20\"). Maximum 20 pages per request."}}}
            }},
          {"required", {"file_path"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
     namespace fs = std::filesystem;
        std::string path = input["file_path"].get<std::string>();
      std::string pages = input.value("pages", "");

        // === SECURITY CHECKS (JackProAi alignment) ===

        // 1. UNC path check - prevent NTLM credential leaks
        if (isUncPath(path)) {
            return ToolResult::fail(
                "Cannot read UNC path: " + path + "\n"
            "UNC paths (\\\\server\\share) are blocked for security.\n"
                "Mount the network share as a local drive first."
         );
        }

        // 2. Device file check - prevent infinite output/blocking
      if (isBlockedDevicePath(path)) {
        return ToolResult::fail(
                "Cannot read '" + path + "': this device file would block or produce infinite output.\n"
                "Device files like /dev/zero, /dev/random, /dev/stdin are not readable."
            );
        }

        // === PATH VALIDATION ===
        fs::path fsPath = utf8Path(path);

        std::error_code ec;
        if (!fs::exists(fsPath, ec) || ec) {
            // Try macOS screenshot alternate path (thin space vs regular space)
      std::string altPath = getAlternateScreenshotPath(path);
            if (!altPath.empty() && fs::exists(utf8Path(altPath))) {
         fsPath = utf8Path(altPath);
                path = altPath;
            } else {
         // Suggest similar files or CWD-relative path
                std::string suggestion;
            std::string similar = findSimilarFile(fsPath);
                if (!similar.empty()) {
                    suggestion = "\nDid you mean: " + similar + "?";
              } else {
                  std::string cwdSuggestion = suggestPathUnderCwd(fsPath, fs::current_path().string());
                  if (!cwdSuggestion.empty()) {
                  suggestion = "\nDid you mean: " + cwdSuggestion + " (relative to cwd)?";
                  }
                }
                return ToolResult::fail("File not found: " + path + suggestion +
                    "\nCurrent working directory: " + fs::current_path().string());
            }
        }

        if (fs::is_directory(fsPath, ec)) {
            return ToolResult::fail("Path is a directory, not a file: " + path);
        }

        // === PERMISSION CHECK (.closecrab/permissions.json deny rules) ===
        // Honors read-deny globs (secrets, keys, .env, node_modules, ...). The
        // manager is loaded once at startup; default rules apply if no config.
        if (PermissionManager::getInstance().isReadDenied(path)) {
            return ToolResult::fail(
                PermissionManager::getInstance().getDenyMessage(path, "read"));
        }

        // === FILE TYPE DETECTION ===
        std::string ext = fsPath.extension().string();
     std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // === PDF HANDLING ===
        if (ext == ".pdf") {
            return handlePdfRead(ctx, path, fsPath, pages);
        }

        // === IMAGE HANDLING ===
      static const std::set<std::string> imageExts = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp"};
        if (imageExts.count(ext) > 0) {
            return handleImageRead(ctx, path, fsPath);
        }

        // === BINARY FILE DETECTION (for text files) ===
        static const std::set<std::string> binaryExts = {
       ".exe", ".dll", ".so", ".dylib", ".bin", ".dat", ".db", ".sqlite",
            ".docx", ".xlsx", ".pptx", ".doc", ".xls", ".ppt",
            ".zip", ".tar", ".gz", ".7z", ".rar", ".bz2",
       ".mp3", ".mp4", ".avi", ".mkv", ".wav", ".flac",
          ".woff", ".woff2", ".ttf", ".otf", ".eot",
            ".pyc", ".pyo", ".class", ".o", ".obj", ".a", ".lib"
        };

        if (binaryExts.count(ext) > 0) {
            return ToolResult::fail(
          "Cannot read binary file: " + path + "\n"
            "File type: " + ext + "\n"
                "Binary files (.docx, .pdf, .jpg, etc.) cannot be read as text.\n"
            "For images, Read tool now supports them directly. For documents, convert to plain text first."
            );
        }

      // Content-based binary detection
        try {
            std::ifstream testFile(fsPath, std::ios::binary);
            if (testFile.is_open()) {
           char buffer[512];
                testFile.read(buffer, sizeof(buffer));
                std::streamsize bytesRead = testFile.gcount();

                if (bytesRead > 0) {
          int nullBytes = 0;
                int controlBytes = 0;
                    for (std::streamsize i = 0; i < bytesRead; i++) {
                        unsigned char c = static_cast<unsigned char>(buffer[i]);
                  if (c == 0) nullBytes++;
                        if (c < 32 && c != 9 && c != 10 && c != 13) controlBytes++;
                    }

                  if (nullBytes > bytesRead / 10 || controlBytes > bytesRead / 3) {
                        return ToolResult::fail(
               "Cannot read binary file: " + path + "\n"
                      "File appears to contain binary data (detected null bytes or excessive control characters).\n"
                 "This file cannot be safely read as text."
              );
                 }
                }
         }
        } catch (...) {}

        // === TEXT FILE HANDLING ===
        return handleTextRead(ctx, path, fsPath, input);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
    return "Read " + input.value("file_path", "file");
    }

private:
    // Record a successful read in the session readFileState so FileWrite/FileEdit
    // can enforce "read before write" for any file type (text, image, PDF).
    void recordReadState(ToolContext& ctx, const std::string& path,
                         const std::filesystem::path& fsPath,
                         bool hasOffset, bool hasLimit) {
        namespace fs = std::filesystem;
        if (!ctx.readFileState) return;
        ToolContext::ReadState rs;
        try {
            std::error_code ec;
            auto mtime = fs::last_write_time(fsPath, ec);
            rs.mtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                mtime.time_since_epoch()).count();
        } catch (...) { rs.mtimeMs = 0; }
        auto [h, sz] = fileContentHash(fsPath);
        rs.contentHash = h;
        rs.contentSize = sz;
        rs.hasOffset = hasOffset;
        rs.hasLimit = hasLimit;
        rs.isPartialView = false;
        (*ctx.readFileState)[normalizePathKey(path)] = rs;
    }

    // === PDF READ HANDLER ===
    ToolResult handlePdfRead(ToolContext& ctx, const std::string& path,
                    const std::filesystem::path& fsPath, const std::string& pages) {
        namespace fs = std::filesystem;

        // === NATIVE PDF MODE (opt-in) ===
        // When CLOSECRAB_PDF_NATIVE=1, send the whole PDF as a base64 `document`
        // block so the model reads text + figures + scanned pages natively
        // (claude-code behavior). OFF by default because many OpenAI-compatible
        // relays don't accept document blocks — the default text-extraction path
        // below works everywhere. Requires an Anthropic-native (or compatible)
        // endpoint. Anthropic limit: ~32MB / 100 pages.
        if (const char* nat = std::getenv("CLOSECRAB_PDF_NATIVE");
            nat && (nat[0] == '1' || nat[0] == 't' || nat[0] == 'T')) {
            std::error_code ec;
            auto sz = fs::file_size(fsPath, ec);
            constexpr size_t PDF_NATIVE_MAX = 32 * 1024 * 1024;
            if (!ec && sz > 0 && sz <= PDF_NATIVE_MAX) {
                std::ifstream pf(fsPath, std::ios::binary);
                if (pf) {
                    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(pf)), {});
                    std::string b64 = base64Encode(bytes);
                    ToolResult r;
                    r.success = true;
                    r.content = "PDF read (native): " + path + " (" +
                                std::to_string(sz / 1024) + "KB) — sent as document for native reading.";
                    r.data = {{"type", "pdf_native"}, {"filePath", path}, {"originalSize", sz}};
                    r.hasContextModification = true;
                    r.contextModification = {
                        {"action", "add_document"},
                        {"media_type", "application/pdf"},
                        {"data", b64}
                    };
                    recordReadState(ctx, path, fsPath, false, false);
                    return r;
                }
            }
            // Too large or unreadable → fall through to text extraction.
        }

        // Validate page range if provided
        std::optional<PdfPageRange> pageRange;
      if (!pages.empty()) {
            pageRange = parsePdfPageRange(pages);
        if (!pageRange.has_value()) {
             return ToolResult::fail(
                 "Invalid pages parameter: \"" + pages + "\". "
                    "Use formats like \"1-5\", \"3\", or \"10-20\". Pages are 1-indexed."
                );
            }

            int rangeSize = (pageRange->lastPage == INT_MAX)
                ? PDF_MAX_PAGES_PER_READ + 1
                : pageRange->lastPage - pageRange->firstPage + 1;

        if (rangeSize > PDF_MAX_PAGES_PER_READ) {
            return ToolResult::fail(
                 "Page range \"" + pages + "\" exceeds maximum of " +
                  std::to_string(PDF_MAX_PAGES_PER_READ) + " pages per request. "
                    "Please use a smaller range."
                );
            }
        }

      // Check page count if no range specified
        if (pages.empty()) {
       int pageCount = getPdfPageCount(fsPath);
            if (pageCount > PDF_MAX_PAGES_PER_READ) {
                return ToolResult::fail(
              "This PDF has " + std::to_string(pageCount) + " pages, which is too many to read at once. "
                  "Use the pages parameter to read specific page ranges (e.g., pages: \"1-5\"). "
                    "Maximum " + std::to_string(PDF_MAX_PAGES_PER_READ) + " pages per request."
                );
       }
        }

        // Extract PDF text
        std::string content = extractPdfText(fsPath, pageRange);

        if (content.find("[Error:") == 0) {
            return ToolResult::fail(content);
        }

        // Add line numbers and return
        std::istringstream stream(content);
        std::string line;
        std::string result;
        int lineNum = 1;
        while (std::getline(stream, line)) {
        result += std::to_string(lineNum++) + "\t" + line + "\n";
        }

        auto fileSize = fs::file_size(fsPath);
        nlohmann::json data = {
            {"type", "pdf"},
            {"filePath", path},
            {"numLines", lineNum - 1},
          {"originalSize", fileSize},
            {"pages", pages.empty() ? "all" : pages}
        };

        recordReadState(ctx, path, fsPath, /*hasOffset*/false, /*hasLimit*/!pages.empty());
        return ToolResult::ok(ensureUtf8(result), data);
    }

    // === IMAGE READ HANDLER ===
    ToolResult handleImageRead(ToolContext& ctx, const std::string& path,
                   const std::filesystem::path& fsPath) {
    namespace fs = std::filesystem;

        std::error_code ec;
        auto fileSize = fs::file_size(fsPath, ec);

        // Decode-bomb guard: refuse absurdly large raw files outright (a 4KB PNG
        // can still decode to gigabytes). Everything else is downscaled/compressed
        // to fit rather than rejected.
        constexpr size_t MAX_RAW_IMAGE_BYTES = 30 * 1024 * 1024; // 30 MB
        if (!ec && fileSize > MAX_RAW_IMAGE_BYTES) {
            return ToolResult::fail(
                "Image file too large: " + std::to_string(fileSize / (1024 * 1024)) + "MB\n"
                "Maximum supported source image is 30MB. Please downscale it first."
            );
        }

        // Read + encode. When stb is bundled, large images are downscaled to
        // <=1568px on the long edge and re-encoded as progressive-quality JPEG
        // to fit the per-image token budget (compress, not reject).
        std::string mediaType;
        size_t originalSize = 0;
        ImageDimensions dims;
#ifdef CLOSECRAB_HAS_STB_IMAGE
        // Budget chosen so images whose raw bytes exceed ~375KB get compressed/
        // resized; smaller images pass through untouched.
        constexpr size_t IMAGE_TOKEN_BUDGET = 600000;
        std::string base64 = readImageAsBase64(fsPath, mediaType, originalSize,
                                               IMAGE_TOKEN_BUDGET, &dims);
#else
        std::string base64 = readImageAsBase64(fsPath, mediaType, originalSize);
#endif

        if (base64.empty()) {
            return ToolResult::fail("Failed to read image: " + path);
        }

        nlohmann::json data = {
            {"type", "image"},
            {"media_type", mediaType},
            {"base64", base64},
            {"originalSize", originalSize}
        };
        if (dims.originalWidth > 0) {
            data["width"] = dims.displayWidth > 0 ? dims.displayWidth : dims.originalWidth;
            data["height"] = dims.displayHeight > 0 ? dims.displayHeight : dims.originalHeight;
            data["originalWidth"] = dims.originalWidth;
            data["originalHeight"] = dims.originalHeight;
        }

     ToolResult result;
        result.success = true;
        result.content = "Image read: " + path + " (" + std::to_string(originalSize / 1024) +
                   "KB, " + mediaType + ")";
        result.data = data;
        result.hasContextModification = true;
        result.contextModification = {
          {"action", "add_image"},
            {"media_type", mediaType},
          {"data", base64}
        };

        recordReadState(ctx, path, fsPath, /*hasOffset*/false, /*hasLimit*/false);
     return result;
    }

    // === TEXT FILE READ HANDLER ===
    ToolResult handleTextRead(ToolContext& ctx, const std::string& path,
               const std::filesystem::path& fsPath,
                     const nlohmann::json& input) {
        namespace fs = std::filesystem;

     int offset = jsonInt(input, "offset", 0);
        int limit = jsonInt(input, "limit", 2000);
        if (limit <= 0) limit = 2000;
        bool hasExplicitLimit = input.contains("limit") && !input["limit"].is_null();

      // === DEDUP CHECK ===
        {
            std::lock_guard<std::mutex> lock(dedupMutex_);
            std::string dedupKey = path + ":" + std::to_string(offset) + ":" + std::to_string(limit);
      auto it = readState_.find(dedupKey);
            if (it != readState_.end()) {
                try {
                    std::error_code ec;
          auto mtime = fs::last_write_time(fsPath, ec);
              if (!ec && mtime == it->second.mtime) {
                  nlohmann::json data = {
                 {"filePath", path}, {"numLines", it->second.numLines},
                      {"startLine", offset + 1}, {"totalLines", it->second.totalLines},
                            {"truncated", false}, {"dedup", true}
               };
                        return ToolResult::ok(
                       "[file unchanged since last read — content already in context]", data);
                    }
                } catch (...) {}
                readState_.erase(it);
            }
     }

        constexpr size_t BYTE_CAP = 1024 * 1024; // 1MB

        // === MMAP-CACHED READ ===
        auto& mmapCache = MmapCache::getInstance();
        const char* mappedData = nullptr;
        size_t mappedSize = 0;

        bool mmapOk = mmapCache.get(path, mappedData, mappedSize);
        if (!mmapOk) {
            mmapOk = mmapCache.map(path, mappedData, mappedSize);
        }

        std::string result;
        int lineNum = 0;
        int linesRead = 0;
        bool truncatedByBytes = false;

        if (mmapOk && mappedData != nullptr && mappedSize > 0) {
            const char* pos = mappedData;
            const char* end = mappedData + mappedSize;

            while (pos < end) {
              const char* lineEnd = static_cast<const char*>(
                    std::memchr(pos, '\n', static_cast<size_t>(end - pos)));
                if (!lineEnd) lineEnd = end;

                if (lineNum >= offset && linesRead < limit) {
                    size_t lineLen = static_cast<size_t>(lineEnd - pos);
                    if (lineLen > 0 && pos[lineLen - 1] == '\r') lineLen--;

            result += std::to_string(lineNum + 1);
                    result += '\t';
                    // Cap absurdly long single lines (claude-code: ~2000 chars/line)
                    // so a minified/one-line file can't blow up the context.
                    if (lineLen > MAX_LINE_CHARS) {
                        result.append(pos, MAX_LINE_CHARS);
                        result += "... [line truncated, " + std::to_string(lineLen) + " chars]";
                    } else {
                        result.append(pos, lineLen);
                    }
                result += '\n';
            linesRead++;

                    if (!hasExplicitLimit && result.size() > BYTE_CAP) {
                        truncatedByBytes = true;
                 lineNum++;
                        break;
                    }
          }
           lineNum++;
                pos = (lineEnd < end) ? lineEnd + 1 : end;

                if (linesRead >= limit && lineNum > offset + limit + 1000) break;
            }
        } else if (mmapOk && (mappedData == nullptr || mappedSize == 0)) {
            // Empty file
        } else {
            // Fallback to ifstream
            std::ifstream file(fsPath, std::ios::binary);
            if (!file.is_open()) {
            return ToolResult::fail("Cannot open file: " + path);
            }

          std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (lineNum >= offset && linesRead < limit) {
                    if (line.size() > MAX_LINE_CHARS) {
                        result += std::to_string(lineNum + 1) + "\t" +
                                  line.substr(0, MAX_LINE_CHARS) +
                                  "... [line truncated, " + std::to_string(line.size()) + " chars]\n";
                    } else {
                        result += std::to_string(lineNum + 1) + "\t" + line + "\n";
                    }
                  linesRead++;
          if (!hasExplicitLimit && result.size() > BYTE_CAP) {
                        truncatedByBytes = true;
                        lineNum++;
                  break;
                  }
                }
                lineNum++;
              if (linesRead >= limit && lineNum > offset + limit + 1000) break;
            }
        }

        int totalLines = lineNum;

        // Release the memory-mapped view now that content is copied into result.
        // On Windows an active file mapping prevents the file from being truncated,
        // overwritten or deleted by any process (editors/compilers and even
        // CloseCrab own write/edit tools). Data is already copied, so unmap now.
        mmapCache.invalidate(path);

        bool truncated = truncatedByBytes || (linesRead >= limit && totalLines > offset + linesRead);
        if (truncated) {
       int nextOffset = offset + linesRead;
            result += "\n[Truncated: showed lines " + std::to_string(offset + 1) + "-" +
                   std::to_string(offset + linesRead) + ". File has more content. " +
                "Read more with offset=" + std::to_string(nextOffset) +
                      (truncatedByBytes ? " (stopped at 1MB)." : ".") + "]\n";
        }

        nlohmann::json data = {
     {"filePath", path},
            {"numLines", linesRead},
            {"startLine", offset + 1},
            {"totalLines", totalLines},
         {"truncated", truncated}
        };

        // Store dedup state
        {
            std::lock_guard<std::mutex> lock(dedupMutex_);
            std::string dedupKey = path + ":" + std::to_string(offset) + ":" + std::to_string(limit);
            DedupEntry entry;
            try {
                std::error_code ec;
                entry.mtime = fs::last_write_time(fsPath, ec);
      } catch (...) {}
            entry.numLines = linesRead;
            entry.totalLines = totalLines;
            readState_[dedupKey] = entry;
        }

        // Update session readFileState
        if (ctx.readFileState) {
            ToolContext::ReadState rs;
         try {
              std::error_code ec;
                auto mtime = fs::last_write_time(fsPath, ec);
                rs.mtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
               mtime.time_since_epoch()).count();
            } catch (...) { rs.mtimeMs = 0; }
            auto [h, sz] = fileContentHash(fsPath);
      rs.contentHash = h;
       rs.contentSize = sz;
            rs.hasOffset = (offset > 0);
         rs.hasLimit = hasExplicitLimit;
            rs.isPartialView = false;
            (*ctx.readFileState)[normalizePathKey(path)] = rs;
     }

        return ToolResult::ok(ensureUtf8(result), data);
    }

    struct DedupEntry {
        std::filesystem::file_time_type mtime;
        int numLines = 0;
        int totalLines = 0;
    };
    mutable std::mutex dedupMutex_;
    mutable std::map<std::string, DedupEntry> readState_;
};

} // namespace closecrab
