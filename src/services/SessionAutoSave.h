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

        // Serialize last 50 messages (avoid saving too much)
        nlohmann::json context = nlohmann::json::array();
        size_t start = messages.size() > 50 ? messages.size() - 50 : 0;
        for (size_t i = start; i < messages.size(); i++) {
            context.push_back(messages[i].toApiJson());
        }

        mgr->updateContext(sessionId, context.dump());
    }

    static bool shouldSave(int turnCount) {
        // Save every 3 turns
        return turnCount % 3 == 0;
    }
};

} // namespace closecrab
