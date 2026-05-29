#pragma once

#include "../Tool.h"
#include "../../utils/StringUtils.h"
#include "../../core/ThreadPool.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <regex>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <array>
#include <unordered_map>
#include <unordered_set>

namespace closecrab {

class GrepTool : public Tool {
public:
    std::string getName() const override { return "Grep"; }
    std::string getDescription() const override {
        return "Search file contents using regex. Supports context lines, file type filters, "
               "and multiple output modes (content, files_with_matches, count).";
    }
    std::string getCategory() const override { return "file"; }
    bool isReadOnly() const override { return true; }
    bool isConcurrencySafe() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"pattern", {{"type", "string"}, {"description", "Regex pattern to search for"}}},
                {"path", {{"type", "string"}, {"description", "File or directory to search"}}},
                {"glob", {{"type", "string"}, {"description", "Glob filter (e.g. \"*.cpp\")"}}},
                {"output_mode", {{"type", "string"}, {"description", "content, files_with_matches, or count"}}},
                {"-i", {{"type", "boolean"}, {"description", "Case insensitive"}}},
                {"-n", {{"type", "boolean"}, {"description", "Show line numbers"}}},
                {"-A", {{"type", "integer"}, {"description", "Lines after match"}}},
                {"-B", {{"type", "integer"}, {"description", "Lines before match"}}},
                {"-C", {{"type", "integer"}, {"description", "Context lines (before and after)"}}},
                {"head_limit", {{"type", "integer"}, {"description", "Max output lines (default 250)"}}},
                {"type", {{"type", "string"}, {"description", "File type filter (e.g. cpp, py, js)"}}}
            }},
            {"required", {"pattern"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        if (isRgAvailable()) return callWithRg(ctx, input);
        return callBuiltIn(ctx, input);
    }

    std::string getActivityDescription(const nlohmann::json& input) const override {
        return "Grep " + input.value("pattern", "...");
    }

private:
    struct FileResult { std::string path; std::string content; int matchCount = 0; };

    static bool isRgAvailable() {
        static int cached = -1;
        if (cached >= 0) return cached == 1;
#ifdef _WIN32
        auto pipe = _popen("rg --version 2>nul", "r");
#else
        auto pipe = popen("rg --version 2>/dev/null", "r");
#endif
        if (!pipe) { cached = 0; return false; }
        char buf[64]; bool got = fgets(buf, sizeof(buf), pipe) != nullptr;
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        cached = got ? 1 : 0;
        return cached == 1;
    }

    // --- rg-based path (fast, used when ripgrep is installed) ---
    ToolResult callWithRg(ToolContext& ctx, const nlohmann::json& input) {
        std::string pattern = input["pattern"].get<std::string>();
        std::string searchPath = input.value("path", ctx.cwd);
        std::string outputMode = input.value("output_mode", "files_with_matches");
        int headLimit = input.value("head_limit", 250);

        std::string cmd = "rg";
        if (outputMode == "files_with_matches") cmd += " -l";
        else if (outputMode == "count") cmd += " -c";
        else if (input.value("-n", true)) cmd += " -n";
        if (input.value("-i", false)) cmd += " -i";
        if (input.contains("-C")) cmd += " -C " + std::to_string(input["-C"].get<int>());
        else {
            if (input.contains("-A")) cmd += " -A " + std::to_string(input["-A"].get<int>());
            if (input.contains("-B")) cmd += " -B " + std::to_string(input["-B"].get<int>());
        }
        if (input.contains("glob")) {
            std::string g = input["glob"].get<std::string>();
            g.erase(std::remove(g.begin(), g.end(), '"'), g.end());
            cmd += " --glob \"" + g + "\"";
        }
        if (input.contains("type")) {
            std::string tf = input["type"].get<std::string>();
            tf.erase(std::remove_if(tf.begin(), tf.end(),
                [](char c) { return !std::isalnum(c) && c != '-'; }), tf.end());
            cmd += " --type " + tf;
        }
        std::string sp = pattern, pp = searchPath;
        std::replace(sp.begin(), sp.end(), '"', '\'');
        std::replace(pp.begin(), pp.end(), '"', '\'');
        cmd += " -- \"" + sp + "\" \"" + pp + "\"";

        std::string output = execCmd(cmd);
        if (output.empty()) return ToolResult::ok("No matches found for pattern: " + pattern);
        if (headLimit > 0) output = limitLines(output, headLimit);
        int lc = (int)std::count(output.begin(), output.end(), '\n');
        return ToolResult::ok(output, {{"mode", outputMode}, {"numLines", lc}, {"appliedLimit", headLimit}});
    }

    // --- Built-in multi-threaded grep (fallback when rg unavailable) ---
    ToolResult callBuiltIn(ToolContext& ctx, const nlohmann::json& input) {
        namespace fs = std::filesystem;
        std::string pattern = input["pattern"].get<std::string>();
        std::string searchPath = input.value("path", ctx.cwd);
        std::string outputMode = input.value("output_mode", "files_with_matches");
        int headLimit = input.value("head_limit", 250);
        bool icase = input.value("-i", false);
        bool lineNums = input.value("-n", true);
        int ctxA = input.value("-A", 0), ctxB = input.value("-B", 0);
        if (input.contains("-C")) ctxA = ctxB = input["-C"].get<int>();

        auto flags = std::regex::ECMAScript | std::regex::optimize;
        if (icase) flags |= std::regex::icase;
        std::regex re;
        try { re = std::regex(pattern, flags); }
        catch (const std::regex_error& e) { return ToolResult::fail("Invalid regex: " + std::string(e.what())); }

        fs::path root = utf8Path(searchPath);
        std::error_code ec;
        if (!fs::exists(root, ec)) return ToolResult::fail("Path not found: " + searchPath);

        // Collect files
        std::vector<fs::path> files;
        if (fs::is_regular_file(root, ec)) { files.push_back(root); }
        else {
            auto exts = typeToExts(input.value("type", ""));
            std::string gf = input.value("glob", "");
            std::regex globRe;
            if (!gf.empty()) { try { globRe = std::regex(globToRe(gf)); } catch (...) { gf.clear(); } }
            for (auto& e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
                if (!e.is_regular_file(ec)) continue;
                std::string name; try { name = e.path().filename().u8string(); } catch (...) { continue; }
                if (!exts.empty()) {
                    std::string ext; try { ext = e.path().extension().u8string(); } catch (...) { continue; }
                    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (exts.find(ext) == exts.end()) continue;
                }
                if (!gf.empty() && !std::regex_match(name, globRe)) continue;
                files.push_back(e.path());
                if (files.size() > 50000) break;
            }
        }
        if (files.empty()) return ToolResult::ok("No matches found for pattern: " + pattern);

        // Parallel search
        std::mutex mtx;
        std::vector<FileResult> results;
        std::atomic<int> totalLines{0};
        auto& pool = ThreadPool::getInstance();
        std::vector<std::future<void>> futs;
        futs.reserve(files.size());

        for (auto& fp : files) {
            futs.push_back(pool.submit([&, fp]() {
                auto fr = grepFile(fp, root, re, outputMode, lineNums, ctxB, ctxA);
                if (fr.matchCount > 0) {
                    std::lock_guard<std::mutex> lk(mtx);
                    totalLines += (int)std::count(fr.content.begin(), fr.content.end(), '\n');
                    if (headLimit <= 0 || totalLines <= headLimit)
                        results.push_back(std::move(fr));
                }
            }));
        }
        for (auto& f : futs) f.get();

        if (results.empty()) return ToolResult::ok("No matches found for pattern: " + pattern);
        std::sort(results.begin(), results.end(),
            [](const FileResult& a, const FileResult& b) { return a.path < b.path; });

        std::string output;
        for (auto& r : results) output += r.content;
        if (headLimit > 0) output = limitLines(output, headLimit);
        int lc = (int)std::count(output.begin(), output.end(), '\n');
        return ToolResult::ok(output, {{"mode", outputMode}, {"numLines", lc}, {"appliedLimit", headLimit}});
    }

    // Grep a single file
    static FileResult grepFile(const std::filesystem::path& fp, const std::filesystem::path& root,
                               const std::regex& re, const std::string& mode,
                               bool lineNums, int ctxB, int ctxA) {
        FileResult fr;
        try { fr.path = std::filesystem::relative(fp, root).u8string(); } catch (...) { fr.path = fp.u8string(); }
        std::replace(fr.path.begin(), fr.path.end(), '\\', '/');

        std::ifstream ifs(fp, std::ios::binary);
        if (!ifs) return fr;
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(ifs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            if (lines.size() > 100000) return fr;
        }

        std::vector<int> hits;
        for (int i = 0; i < (int)lines.size(); i++)
            if (std::regex_search(lines[i], re)) hits.push_back(i);
        fr.matchCount = (int)hits.size();
        if (fr.matchCount == 0) return fr;

        if (mode == "files_with_matches") { fr.content = fr.path + "\n"; return fr; }
        if (mode == "count") { fr.content = fr.path + ":" + std::to_string(fr.matchCount) + "\n"; return fr; }

        // Content mode with context lines
        std::unordered_set<int> show;
        for (int idx : hits)
            for (int j = std::max(0, idx - ctxB); j <= std::min((int)lines.size()-1, idx + ctxA); j++)
                show.insert(j);

        std::string buf; buf.reserve(fr.matchCount * 80);
        int prev = -2;
        for (int i = 0; i < (int)lines.size(); i++) {
            if (show.find(i) == show.end()) continue;
            if (prev >= 0 && i > prev + 1) buf += "--\n";
            buf += fr.path + ":";
            if (lineNums) buf += std::to_string(i + 1) + ":";
            buf += lines[i] + "\n";
            prev = i;
        }
        fr.content = std::move(buf);
        return fr;
    }

    // --- Utilities ---
    static std::string execCmd(const std::string& cmd) {
        std::string result;
#ifdef _WIN32
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif
        if (!pipe) return "";
        std::array<char, 4096> buf;
        while (fgets(buf.data(), buf.size(), pipe.get())) result += buf.data();
        return result;
    }

    static std::string limitLines(const std::string& s, int max) {
        std::string out; int n = 0;
        for (size_t i = 0; i < s.size(); i++) { out += s[i]; if (s[i] == '\n' && ++n >= max) break; }
        return out;
    }

    static std::unordered_set<std::string> typeToExts(const std::string& type) {
        static const std::unordered_map<std::string, std::vector<std::string>> m = {
            {"cpp",{"cpp","cxx","cc","c","h","hpp","hxx","inl"}}, {"c",{"c","h"}},
            {"py",{"py","pyi","pyw"}}, {"js",{"js","mjs","cjs","jsx"}}, {"ts",{"ts","tsx","mts","cts"}},
            {"java",{"java"}}, {"rust",{"rs"}}, {"go",{"go"}}, {"rb",{"rb","erb"}}, {"cs",{"cs"}},
            {"swift",{"swift"}}, {"kotlin",{"kt","kts"}}, {"html",{"html","htm","xhtml"}},
            {"css",{"css","scss","sass","less"}}, {"json",{"json","jsonc","jsonl"}},
            {"yaml",{"yaml","yml"}}, {"xml",{"xml","xsl","xslt","svg"}}, {"md",{"md","markdown"}},
            {"sh",{"sh","bash","zsh","fish"}}, {"sql",{"sql"}}, {"cmake",{"cmake"}},
        };
        std::unordered_set<std::string> exts;
        if (type.empty()) return exts;
        std::string lower = type;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        auto it = m.find(lower);
        if (it != m.end()) for (auto& e : it->second) exts.insert(e);
        else exts.insert(lower);
        return exts;
    }

    static std::string globToRe(const std::string& glob) {
        std::string r;
        for (char c : glob) {
            if (c == '*') r += ".*"; else if (c == '?') r += ".";
            else if (c == '.' || c == '+' || c == '(' || c == ')' || c == '[' || c == ']' ||
                     c == '{' || c == '}' || c == '^' || c == '$' || c == '|' || c == '\\')
                { r += '\\'; r += c; }
            else r += c;
        }
        return r;
    }
};

} // namespace closecrab
