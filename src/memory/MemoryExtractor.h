#pragma once

#include "../api/APIClient.h"
#include "../memory/FileMemoryManager.h"
#include "../core/Message.h"
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

namespace closecrab {

// Automatically extracts memorable information from conversations
// Mirrors JackProAi's extractMemories service
class MemoryExtractor {
public:
    // Extract memories from recent messages and save to FileMemoryManager
    static int extractAndSave(const std::vector<Message>& messages,
                               APIClient* apiClient,
                               const std::string& projectRoot) {
        if (!apiClient || messages.size() < 6) return 0;

        // Build conversation summary for extraction
        std::string conversation;
        int start = messages.size() > 20 ? (int)messages.size() - 20 : 0;
        for (int i = start; i < (int)messages.size(); i++) {
            const auto& m = messages[i];
            std::string role = (m.role == MessageRole::USER) ? "User" : "Assistant";
            std::string text = m.getText();
            if (text.size() > 300) text = text.substr(0, 300) + "...";
            if (!text.empty()) conversation += role + ": " + text + "\n";
        }

        if (conversation.empty()) return 0;

        std::string prompt =
            "Analyze this conversation and extract information worth remembering for future sessions.\n"
            "For each memory, output a JSON object on its own line:\n"
            "{\"type\": \"user|feedback|project|reference\", \"name\": \"short_name\", "
            "\"description\": \"one line\", \"content\": \"details\"}\n\n"
            "Types:\n"
            "- user: info about the user (role, preferences, expertise)\n"
            "- feedback: corrections or confirmed approaches\n"
            "- project: ongoing work, goals, decisions\n"
            "- reference: pointers to external resources\n\n"
            "Only extract genuinely useful information. Output nothing if nothing is worth remembering.\n\n"
            "Conversation:\n" + conversation;

        try {
            std::vector<Message> msgs = {Message::makeUser(prompt)};
            ModelConfig cfg;
            cfg.maxTokens = 1024;
            cfg.temperature = 0.3f;
            std::string response = apiClient->chat(msgs,
                "You extract memories from conversations. Output JSON lines only.", cfg);

            // Parse JSON lines from response
            FileMemoryManager mgr(projectRoot);
            int saved = 0;
            std::istringstream iss(response);
            std::string line;
            while (std::getline(iss, line)) {
                auto start = line.find('{');
                auto end = line.rfind('}');
                if (start == std::string::npos || end == std::string::npos) continue;
                try {
                    auto j = nlohmann::json::parse(line.substr(start, end - start + 1));
                    std::string type = j.value("type", "project");
                    std::string name = j.value("name", "memory_" + std::to_string(saved));
                    std::string desc = j.value("description", "");
                    std::string content = j.value("content", "");
                    if (name.empty() || content.empty()) continue;

                    // Sanitize filename
                    std::string filename = type + "_" + name;
                    for (char& c : filename) {
                        if (!std::isalnum(c) && c != '_' && c != '-') c = '_';
                    }

                    mgr.saveMemory(filename, name, desc, parseMemoryType(type), content);
                    saved++;
                } catch (...) {}
            }

            if (saved > 0) {
                spdlog::info("Extracted {} memories from conversation", saved);
            }
            return saved;
        } catch (const std::exception& e) {
            spdlog::warn("Memory extraction failed: {}", e.what());
            return 0;
        }
    }
};

} // namespace closecrab
