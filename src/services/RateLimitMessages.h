#pragma once
#include "../api/APIError.h"
#include <string>

namespace closecrab {

class RateLimitMessages {
public:
    static std::string format(const APIError& error) {
        switch (error.type) {
            case APIErrorType::RATE_LIMIT: {
                std::string msg = error.what();
                int retryAfter = parseRetryAfterFromMessage(msg);
                if (retryAfter > 0) {
                    return "Rate limited. " + countdown(retryAfter);
                }
                return "Rate limited. Waiting before retry...";
            }
            case APIErrorType::OVERLOADED:
                return "API is overloaded. Will retry shortly...";
            case APIErrorType::SERVER_ERROR:
                return "Server error (HTTP " + std::to_string(error.httpStatus) + "). Retrying...";
            case APIErrorType::NETWORK_ERROR:
                return "Network error. Check your connection and retry.";
            case APIErrorType::AUTH_ERROR:
                return "Authentication failed. Check your API key.";
            case APIErrorType::CONTEXT_TOO_LONG:
                return "Context too long. Try compacting history with /compact.";
            case APIErrorType::INVALID_REQUEST:
                return "Invalid request. " + std::string(error.what());
            default:
                return "API error: " + std::string(error.what());
        }
    }

    static int parseRetryAfter(const std::string& headerValue) {
        // Try parsing as integer (seconds)
        try {
            return std::stoi(headerValue);
        } catch (...) {}
        // Could also parse HTTP date format, but seconds is most common
        return 0;
    }

    static std::string countdown(int secondsRemaining) {
        if (secondsRemaining <= 0) return "Retrying now...";
        if (secondsRemaining < 60) {
            return "Retrying in " + std::to_string(secondsRemaining) + "s...";
        }
        int min = secondsRemaining / 60;
        int sec = secondsRemaining % 60;
        return "Retrying in " + std::to_string(min) + "m " + std::to_string(sec) + "s...";
    }

private:
    static int parseRetryAfterFromMessage(const std::string& msg) {
        // Try to extract retry-after from error message
        auto pos = msg.find("retry");
        if (pos == std::string::npos) pos = msg.find("Retry");
        if (pos == std::string::npos) return 0;
        // Look for a number after "retry"
        for (size_t i = pos; i < msg.size(); i++) {
            if (std::isdigit(msg[i])) {
                try { return std::stoi(msg.substr(i)); } catch (...) {}
            }
        }
        return 0;
    }
};

} // namespace closecrab
