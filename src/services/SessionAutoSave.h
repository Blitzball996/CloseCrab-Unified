#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/SessionManager.h"
#include "../core/Message.h"

namespace closecrab {

class SessionAutoSave {
public:
    static void save(SessionManager* mgr, const std::string& sessionId,
                     const std::vector<Message>& messages) {
        if (!mgr || sessionId.empty() || messages.empty()) return;

        // Serialize last 50 messages in the SAME shape as
        // QueryEngine::serializeMessages (role + flat text), so it round-trips
        // through deserializeMessages (which reads the top-level "text" field).
        // The previous toApiJson() form had no top-level "text" → deserialize
        // skipped every message → /resume restored 0 (and the list showed empty).
        nlohmann::json context = nlohmann::json::array();
        size_t start = messages.size() > 50 ? messages.size() - 50 : 0;
        for (size_t i = start; i < messages.size(); i++) {
            const auto& msg = messages[i];
            nlohmann::json m;
            m["role"] = (msg.role == MessageRole::USER) ? "user" :
                        (msg.role == MessageRole::ASSISTANT) ? "assistant" : "system";
            m["text"] = msg.getText();
            m["timestamp"] = msg.timestamp;
            context.push_back(std::move(m));
        }

        mgr->updateContext(sessionId, context.dump());
    }

    static bool shouldSave(int turnCount) {
        // Save every 3 turns
        return turnCount % 3 == 0;
    }
};

} // namespace closecrab
