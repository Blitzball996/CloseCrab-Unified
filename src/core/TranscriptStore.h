#pragma once

// TranscriptStore — append-only JSONL session transcripts, translated from
// JackProAi's utils/sessionStorage.ts behavior (one .jsonl per session, each
// message appended as a line as it happens; resume reads the file back).
//
// Why JSONL append instead of CloseCrab's old "dump whole context into a SQLite
// column each turn": the append log survives a crash mid-session (every line is
// already flushed), avoids overwrite races, and matches JackProAi's process.
// Behavior-equivalent, not byte-identical to JackProAi's entry schema.

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "Message.h"

namespace closecrab {

class TranscriptStore {
public:
    // Per-project isolation: transcripts live under a bucket keyed by the user's
    // project directory, so /resume in project A never shows project B's history.
    // The process chdir's to a shared app-data dir at startup, so a bare
    // "data/transcripts" would mix every project together (the old behavior — the
    // "resume 串项目" bug). setProject() is called once from main() with the user's
    // launch cwd; the key is a short stable hash of that path plus a readable
    // folder-name suffix so the buckets are eyeball-identifiable on disk.
    static void setProject(const std::string& projectCwd) {
        projectKey() = makeProjectKey(projectCwd);
    }

    // <cwd>/data/transcripts[/<projectKey>]/<sessionId>.jsonl
    static std::filesystem::path dir() {
        std::filesystem::path base = std::filesystem::path("data") / "transcripts";
        const std::string& key = projectKey();
        return key.empty() ? base : (base / key);
    }
    static std::filesystem::path pathFor(const std::string& sessionId) {
        return dir() / (sessionId + ".jsonl");
    }

    // Serialize ONE message to a JSONL line. We store the FULL message — role +
    // content blocks (tool_use / tool_result / thinking / text) via toApiJson —
    // plus a flat "text" mirror and the timestamp. Older transcripts only had
    // {role,text}; load() handles both shapes (see below). Storing the blocks is
    // what lets /resume replay tool calls, not just the plain chatter.
    static nlohmann::json messageToEntry(const Message& msg) {
        nlohmann::json m = msg.toApiJson();   // { role, content:[...] }
        m["text"] = msg.getText();            // flat mirror for cheap previews
        m["timestamp"] = msg.timestamp;
        // Preserve system role: toApiJson() collapses non-user to "assistant",
        // but transcripts want to distinguish system/local-command lines.
        if (msg.role == MessageRole::SYSTEM) m["role"] = "system";
        return m;
    }

    // True if a message carries anything worth persisting: visible text OR any
    // content block (a pure tool_use assistant turn has empty getText() but MUST
    // be kept, otherwise the restored transcript loses the tool call entirely).
    static bool isMeaningful(const Message& msg) {
        if (!msg.getText().empty()) return true;
        return !msg.content.empty();
    }

    // Append messages[fromIndex..end] as JSONL lines (append-only, like
    // fsAppendFile). Returns the new persisted count (= messages.size()).
    static size_t appendDelta(const std::string& sessionId,
                              const std::vector<Message>& messages,
                              size_t fromIndex) {
        if (sessionId.empty() || fromIndex >= messages.size()) return messages.size();
        std::error_code ec;
        std::filesystem::create_directories(dir(), ec);
        std::ofstream f(pathFor(sessionId), std::ios::app | std::ios::binary);
        if (!f.is_open()) return fromIndex;
        for (size_t i = fromIndex; i < messages.size(); i++) {
            // Skip truly-empty messages, but KEEP tool_use/tool_result turns even
            // when their flat text is empty (previously these were dropped).
            if (!isMeaningful(messages[i])) continue;
            f << messageToEntry(messages[i]).dump(-1, ' ', false,
                  nlohmann::json::error_handler_t::replace) << "\n";
        }
        return messages.size();
    }

    // Load a session transcript back into Message objects (resume). Handles both
    // the new shape ({role,content:[...],text}) and the legacy shape
    // ({role,text} only) transparently.
    static std::vector<Message> load(const std::string& sessionId) {
        std::vector<Message> out;
        std::filesystem::path p = pathFor(sessionId);
        std::error_code ec;
        // Legacy fallback: sessions created before per-project bucketing live in
        // the flat data/transcripts/ dir. If the id isn't in this project's
        // bucket, look there too, so an explicit /resume <old-id> still works.
        if (!projectKey().empty() && !std::filesystem::exists(p, ec)) {
            std::filesystem::path legacy =
                std::filesystem::path("data") / "transcripts" / (sessionId + ".jsonl");
            if (std::filesystem::exists(legacy, ec)) p = legacy;
        }
        std::ifstream f(p, std::ios::binary);
        if (!f.is_open()) return out;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            try {
                auto m = nlohmann::json::parse(line);
                std::string role = m.value("role", "user");
                // New shape: a content array is present -> reconstruct full blocks.
                bool hasBlockArray = m.contains("content") && m["content"].is_array();
                if (hasBlockArray) {
                    Message msg = Message::fromApiJson(m);
                    // fromApiJson() maps non-assistant to USER; fix up system rows.
                    if (role == "system") {
                        msg.role = MessageRole::SYSTEM;
                        msg.type = MessageType::SYSTEM;
                    }
                    if (m.contains("timestamp") && m["timestamp"].is_number())
                        msg.timestamp = m["timestamp"].get<int64_t>();
                    // Keep it only if it carries something.
                    if (!msg.content.empty() || !msg.getText().empty())
                        out.push_back(std::move(msg));
                    continue;
                }
                // Legacy shape: flat text only.
                std::string text = m.value("text", "");
                if (text.empty()) continue;
                if (role == "user") out.push_back(Message::makeUser(text));
                else if (role == "assistant") out.push_back(Message::makeAssistant(text));
                else out.push_back(Message::makeSystem(SystemSubtype::LOCAL_COMMAND, text));
            } catch (...) { /* skip malformed line */ }
        }
        return out;
    }

    struct Info {
        std::string sessionId;
        std::string firstUserPreview;
        int messageCount = 0;
        int64_t mtime = 0;
    };

    // List recent sessions by scanning the transcript dir, newest first.
    static std::vector<Info> list(int limit = 10) {
        std::vector<Info> infos;
        std::error_code ec;
        if (!std::filesystem::exists(dir(), ec)) return infos;
        for (auto& e : std::filesystem::directory_iterator(dir(), ec)) {
            if (!e.is_regular_file()) continue;
            auto p = e.path();
            if (p.extension() != ".jsonl") continue;
            Info info;
            info.sessionId = p.stem().string();
            // Cheap pass: count lines + first user preview + last timestamp.
            std::ifstream f(p, std::ios::binary);
            std::string line;
            while (std::getline(f, line)) {
                if (line.empty()) continue;
                info.messageCount++;
                try {
                    auto m = nlohmann::json::parse(line);
                    // Use the stored Unix-ms timestamp (accurate), not the file
                    // mtime (file_time_type epoch != Unix epoch → wrong year).
                    if (m.contains("timestamp")) {
                        int64_t ts = m["timestamp"].get<int64_t>() / 1000; // ms → s
                        if (ts > info.mtime) info.mtime = ts;
                    }
                    if (info.firstUserPreview.empty() && m.value("role", "") == "user") {
                        std::string t = m.value("text", "");
                        if (t.size() > 50) t = t.substr(0, 50) + "...";
                        info.firstUserPreview = t;
                    }
                } catch (...) {}
            }
            if (info.messageCount > 0) infos.push_back(std::move(info));
        }
        std::sort(infos.begin(), infos.end(),
                  [](const Info& a, const Info& b) { return a.mtime > b.mtime; });
        if ((int)infos.size() > limit) infos.resize(limit);
        return infos;
    }

private:
    // Process-wide project bucket key. Empty = un-set (falls back to the flat
    // dir, preserving old single-project behavior until setProject() is called).
    static std::string& projectKey() {
        static std::string key;
        return key;
    }

    // Build a stable, filesystem-safe bucket name from the project path:
    //   "<8-hex-hash>__<sanitized-leaf-folder-name>"
    // The hash disambiguates same-named folders in different locations; the
    // suffix keeps the bucket human-recognizable (e.g. "a3f9c1d2__CloseCrab-Unified").
    static std::string makeProjectKey(const std::string& projectCwd) {
        if (projectCwd.empty()) return "";
        // Normalize the path so the same project always hashes identically
        // regardless of trailing slash or separator style.
        std::string norm = projectCwd;
        for (char& c : norm) if (c == '\\') c = '/';
        while (norm.size() > 1 && norm.back() == '/') norm.pop_back();
#ifdef _WIN32
        // Windows paths are case-insensitive — lowercase so C:\X and c:\x match.
        for (char& c : norm) c = (char)std::tolower((unsigned char)c);
#endif
        // FNV-1a 32-bit — tiny, dependency-free, stable across runs/platforms.
        uint32_t h = 2166136261u;
        for (unsigned char c : norm) { h ^= c; h *= 16777619u; }
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%08x", h);

        // Readable suffix from the last path segment.
        std::string leaf;
        auto pos = norm.find_last_of('/');
        leaf = (pos == std::string::npos) ? norm : norm.substr(pos + 1);
        std::string safe;
        for (char c : leaf) {
            if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') safe += c;
            else safe += '_';
        }
        if (safe.size() > 40) safe.resize(40);
        if (safe.empty()) safe = "project";
        return std::string(hex) + "__" + safe;
    }
};

} // namespace closecrab
