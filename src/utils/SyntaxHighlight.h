#pragma once

// SyntaxHighlight — lightweight terminal syntax highlighter. JackProAi uses
// cli-highlight (highlight.js); C++ has no equivalent, so this is a small
// hand-rolled tokenizer that colors the things that matter for terminal reading:
// strings, line/block comments, language keywords, and numbers. Behavior-
// equivalent ("readable colored code"), implementation different.
//
// Languages: C/C++, JS/TS, Python, JSON, Bash, CMake (keyword sets below).
// Unknown language → strings/comments/numbers only (still useful).

#include <string>
#include <vector>
#include <unordered_set>
#include <cctype>

namespace closecrab {

class SyntaxHighlight {
public:
    // 256-color ANSI: keyword=176(purple) string=114(green) comment=245(gray)
    // number=215(orange). reset at end.
    static constexpr const char* KW  = "\033[38;5;176m";
    static constexpr const char* STR = "\033[38;5;114m";
    static constexpr const char* CMT = "\033[38;5;245m";
    static constexpr const char* NUM = "\033[38;5;215m";
    static constexpr const char* RST = "\033[0m";

    // Map a fenced-code language tag or file extension to a keyword set id.
    static std::string langFromHint(const std::string& hint) {
        std::string h;
        for (char c : hint) h.push_back((char)std::tolower((unsigned char)c));
        if (h=="c"||h=="cpp"||h=="cc"||h=="h"||h=="hpp"||h=="c++"||h=="cxx") return "cpp";
        if (h=="js"||h=="ts"||h=="jsx"||h=="tsx"||h=="javascript"||h=="typescript") return "js";
        if (h=="py"||h=="python") return "py";
        if (h=="json") return "json";
        if (h=="sh"||h=="bash"||h=="shell"||h=="zsh") return "bash";
        if (h=="cmake") return "cmake";
        return "";
    }

    // Highlight one line (no trailing newline). `lang` from langFromHint.
    // `inBlockComment` is in/out state for C-style /* ... */ spanning lines.
    static std::string line(const std::string& src, const std::string& lang,
                            bool& inBlockComment) {
        const auto& kws = keywords(lang);
        std::string out;
        size_t i = 0, n = src.size();

        // Continuing a block comment from a previous line.
        if (inBlockComment) {
            size_t end = src.find("*/");
            if (end == std::string::npos) {
                return std::string(CMT) + src + RST; // whole line still comment
            }
            out += CMT; out += src.substr(0, end + 2); out += RST;
            i = end + 2;
            inBlockComment = false;
        }

        while (i < n) {
            char c = src[i];

            // Line comment: // (C/JS) or # (py/bash/cmake)
            bool slashSlash = (c == '/' && i + 1 < n && src[i+1] == '/');
            bool hashComment = (c == '#' && (lang=="py"||lang=="bash"||lang=="cmake"||lang==""));
            if (slashSlash || hashComment) {
                out += CMT; out += src.substr(i); out += RST;
                break;
            }
            // Block comment start /* (C/JS)
            if (c == '/' && i + 1 < n && src[i+1] == '*') {
                size_t end = src.find("*/", i + 2);
                if (end == std::string::npos) {
                    out += CMT; out += src.substr(i); out += RST;
                    inBlockComment = true;
                    break;
                }
                out += CMT; out += src.substr(i, end + 2 - i); out += RST;
                i = end + 2;
                continue;
            }
            // String literal " ' ` (with backslash escapes)
            if (c == '"' || c == '\'' || c == '`') {
                char q = c;
                size_t j = i + 1;
                while (j < n) {
                    if (src[j] == '\\' && j + 1 < n) { j += 2; continue; }
                    if (src[j] == q) { j++; break; }
                    j++;
                }
                out += STR; out += src.substr(i, j - i); out += RST;
                i = j;
                continue;
            }
            // Number
            if (std::isdigit((unsigned char)c) ||
                (c == '.' && i + 1 < n && std::isdigit((unsigned char)src[i+1]))) {
                size_t j = i;
                while (j < n && (std::isalnum((unsigned char)src[j]) ||
                                 src[j]=='.'||src[j]=='x'||src[j]=='_')) j++;
                out += NUM; out += src.substr(i, j - i); out += RST;
                i = j;
                continue;
            }
            // Identifier / keyword
            if (std::isalpha((unsigned char)c) || c == '_') {
                size_t j = i;
                while (j < n && (std::isalnum((unsigned char)src[j]) || src[j]=='_')) j++;
                std::string word = src.substr(i, j - i);
                if (kws.count(word)) { out += KW; out += word; out += RST; }
                else out += word;
                i = j;
                continue;
            }
            // Other char
            out += c;
            i++;
        }
        return out;
    }

private:
    static const std::unordered_set<std::string>& keywords(const std::string& lang) {
        static const std::unordered_set<std::string> empty;
        static const std::unordered_set<std::string> cpp = {
            "int","char","bool","void","float","double","long","short","unsigned","signed",
            "const","static","struct","class","enum","union","namespace","template","typename",
            "public","private","protected","virtual","override","return","if","else","for","while",
            "do","switch","case","default","break","continue","new","delete","this","nullptr","true",
            "false","auto","using","include","define","ifdef","ifndef","endif","pragma","sizeof",
            "try","catch","throw","constexpr","inline","extern","std","size_t"};
        static const std::unordered_set<std::string> js = {
            "var","let","const","function","return","if","else","for","while","do","switch","case",
            "default","break","continue","new","delete","this","null","undefined","true","false",
            "class","extends","import","export","from","async","await","try","catch","throw","typeof",
            "instanceof","of","in","yield","static","get","set","void","interface","type","enum"};
        static const std::unordered_set<std::string> py = {
            "def","class","return","if","elif","else","for","while","break","continue","import","from",
            "as","with","try","except","finally","raise","lambda","yield","global","nonlocal","pass",
            "None","True","False","and","or","not","in","is","async","await","assert","del","self"};
        static const std::unordered_set<std::string> json = {"true","false","null"};
        static const std::unordered_set<std::string> bash = {
            "if","then","else","elif","fi","for","while","do","done","case","esac","function","return",
            "in","select","until","echo","export","local","readonly","set","unset","source","alias"};
        static const std::unordered_set<std::string> cmake = {
            "if","else","elseif","endif","foreach","endforeach","while","endwhile","function",
            "endfunction","macro","endmacro","set","unset","include","add_executable","add_library",
            "target_link_libraries","find_package","project","cmake_minimum_required","option"};
        if (lang=="cpp") return cpp;
        if (lang=="js") return js;
        if (lang=="py") return py;
        if (lang=="json") return json;
        if (lang=="bash") return bash;
        if (lang=="cmake") return cmake;
        return empty;
    }
};

} // namespace closecrab
