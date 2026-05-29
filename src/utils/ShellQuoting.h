#pragma once

#include <string>

namespace closecrab {

// =============================================================================
// Shell command quoting — mirrors JackProAi src/utils/bash/shellQuoting.ts and
// the bashProvider.ts `eval '<quoted>'` invocation strategy.
//
// Why this exists: CloseCrab launches Git Bash via CreateProcessA with a SINGLE
// command line string ("bash.exe" -c "<cmd>"). When <cmd> itself contains double
// quotes (e.g. `node -e "const fs=require('fs')"`), the inner quotes terminate
// the outer -c "..." wrapper and the MSVCRT argv parser shreds the command —
// exactly the bug.txt failures (node -e / sed -i / python -c "Unexpected end of
// input"). JackProAi avoids this by (1) rewriting Windows `>nul` redirects, then
// (2) wrapping the command in `eval '<single-quote-escaped>'`, then (3) passing
// it as a single argv element. We replicate all three.
// =============================================================================

// JackProAi shellQuoting.ts:124-128 — rewrite Windows CMD `>nul` redirects to
// POSIX `/dev/null`. The model sometimes hallucinates `2>nul`; in Git Bash that
// creates a literal reserved-name file `nul` that breaks git. Matches `>nul`,
// `2>nul`, `&>nul`, `>>nul` (case-insensitive); leaves `>null`, `>nul.txt` alone.
inline std::string rewriteWindowsNullRedirect(const std::string& command) {
    std::string out;
    out.reserve(command.size());
    size_t i = 0;
    const size_t n = command.size();
    while (i < n) {
        // Try to match the redirect-operator prefix: optional digit, optional &,
        // one or more '>', optional spaces.
        size_t j = i;
        if (j < n && std::isdigit((unsigned char)command[j])) j++;
        if (j < n && command[j] == '&') j++;
        size_t gtStart = j;
        while (j < n && command[j] == '>') j++;
        bool hasGt = (j > gtStart);
        if (hasGt) {
            size_t k = j;
            while (k < n && (command[k] == ' ' || command[k] == '\t')) k++;
            // Match NUL (case-insensitive) followed by a delimiter or end.
            if (k + 3 <= n) {
                char a = (char)std::tolower((unsigned char)command[k]);
                char b = (char)std::tolower((unsigned char)command[k+1]);
                char c = (char)std::tolower((unsigned char)command[k+2]);
                bool isNul = (a == 'n' && b == 'u' && c == 'l');
                char after = (k + 3 < n) ? command[k+3] : '\0';
                bool delim = (after == '\0' || after == ' ' || after == '\t' ||
                              after == '|' || after == '&' || after == ';' ||
                              after == ')' || after == '\n');
                if (isNul && delim) {
                    // Emit the operator prefix verbatim, then /dev/null.
                    out.append(command, i, j - i);
                    out += "/dev/null";
                    i = k + 3;
                    continue;
                }
            }
        }
        out.push_back(command[i]);
        i++;
    }
    return out;
}

// JackProAi shellQuoting.ts:46-74 (non-heredoc path) — single-quote-escape the
// command so it survives as one token, then wrap in `eval '...'`. Single quotes
// inside are escaped as '\'' (close, literal-quote, reopen). This is what makes
// `node -e "..."` run verbatim regardless of inner double quotes.
inline std::string buildEvalCommand(const std::string& command) {
    std::string escaped;
    escaped.reserve(command.size() + 16);
    for (char c : command) {
        if (c == '\'') escaped += "'\\''";
        else escaped.push_back(c);
    }
    return "eval '" + escaped + "'";
}

// Escape a single argument for the Windows CreateProcess command line, following
// the MSVCRT / CommandLineToArgvW rules so the child receives it as exactly one
// argv element. Wraps the result in double quotes. This is what Node's
// child_process.spawn does per-argument internally; we do it explicitly because
// CreateProcessA takes one flat command line.
inline std::string windowsQuoteArg(const std::string& arg) {
    std::string out;
    out.push_back('"');
    size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            backslashes++;
        } else if (c == '"') {
            // Backslashes before a quote must be doubled, then escape the quote.
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            backslashes = 0;
            out.push_back(c);
        }
    }
    // Trailing backslashes before the closing quote must be doubled.
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

// Bash-level single-quote escaping for a path/arg appended to a command string
// (used for the `pwd -P > '<file>'` cwd-capture redirect). Single quotes inside
// become '\'' (close, literal-quote, reopen).
inline std::string bashSingleQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

// Convert a Windows path (C:\Users\x) to the MSYS/Git Bash POSIX form (/c/Users/x)
// so bash redirects resolve correctly. UNC \\server\share -> //server/share.
inline std::string windowsToPosixPath(const std::string& winPath) {
    std::string p = winPath;
    for (char& c : p) if (c == '\\') c = '/';
    // Drive letter: C:/... -> /c/...
    if (p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':') {
        std::string rest = (p.size() > 2) ? p.substr(2) : "";
        std::string drive(1, (char)std::tolower((unsigned char)p[0]));
        return "/" + drive + rest;
    }
    return p;
}

// Inverse of windowsToPosixPath: /c/Users/x -> C:\Users\x. Used to feed bash's
// `pwd -P` output back into CreateProcessA's lpCurrentDirectory.
inline std::string posixToWindowsPath(const std::string& posixPath) {
    // /c/... -> C:\...
    if (posixPath.size() >= 3 && posixPath[0] == '/' &&
        std::isalpha((unsigned char)posixPath[1]) &&
        (posixPath[2] == '/' )) {
        std::string out;
        out.push_back((char)std::toupper((unsigned char)posixPath[1]));
        out += ":";
        out += posixPath.substr(2);  // includes leading '/'
        for (char& c : out) if (c == '/') c = '\\';
        return out;
    }
    // /c (drive root, no trailing slash)
    if (posixPath.size() == 2 && posixPath[0] == '/' &&
        std::isalpha((unsigned char)posixPath[1])) {
        std::string out;
        out.push_back((char)std::toupper((unsigned char)posixPath[1]));
        out += ":\\";
        return out;
    }
    std::string p = posixPath;
    for (char& c : p) if (c == '/') c = '\\';
    return p;
}

} // namespace closecrab
