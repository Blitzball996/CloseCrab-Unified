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
#include <nlohmann/json.hpp>
#include "Message.h"

namespace closecrab {

class TranscriptStore {
public:
    // <cwd>/data/transcripts/<sessionId>.jsonl
    static std::filesystem::path dir() {
        return std::filesystem::path("data") / "transcripts";
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
        std::ifstream f(pathFor(sessionId), std::ios::binary);
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
};

} // namespace closecrab
