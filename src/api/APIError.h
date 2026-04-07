#pragma once

#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

namespace closecrab {

enum class APIErrorType {
    AUTH_ERROR,         // 401, 403
    RATE_LIMIT,         // 429
    OVERLOADED,         // 529
    SERVER_ERROR,       // 500, 502, 503
    NETWORK_ERROR,      // Connection failed, timeout
    INVALID_REQUEST,    // 400
    CONTEXT_TOO_LONG,   // 413 or specific error message
    UNKNOWN
};

inline std::string apiErrorTypeName(APIErrorType t) {
    switch (t) {
        case APIErrorType::AUTH_ERROR: return "AuthError";
        case APIErrorType::RATE_LIMIT: return "RateLimitError";
        case APIErrorType::OVERLOADED: return "OverloadedError";
        case APIErrorType::SERVER_ERROR: return "ServerError";
        case APIErrorType::NETWORK_ERROR: return "NetworkError";
        case APIErrorType::INVALID_REQUEST: return "InvalidRequestError";
        case APIErrorType::CONTEXT_TOO_LONG: return "ContextTooLongError";
        default: return "UnknownError";
    }
}

struct APIError : std::runtime_error {
    APIErrorType type;
    int httpStatus;

    APIError(APIErrorType type, int httpStatus, const std::string& message)
        : std::runtime_error(apiErrorTypeName(type) + " (" + std::to_string(httpStatus) + "): " + message)
        , type(type), httpStatus(httpStatus) {}
};

inline APIErrorType classifyHttpStatus(long httpStatus) {
    switch (httpStatus) {
        case 400: return APIErrorType::INVALID_REQUEST;
        case 401: case 403: return APIErrorType::AUTH_ERROR;
        case 413: return APIErrorType::CONTEXT_TOO_LONG;
        case 429: return APIErrorType::RATE_LIMIT;
        case 500: case 502: case 503: return APIErrorType::SERVER_ERROR;
        case 529: return APIErrorType::OVERLOADED;
        default:
            if (httpStatus >= 500) return APIErrorType::SERVER_ERROR;
            return APIErrorType::UNKNOWN;
    }
}

inline bool isRetryable(APIErrorType type) {
    return type == APIErrorType::RATE_LIMIT
        || type == APIErrorType::OVERLOADED
        || type == APIErrorType::SERVER_ERROR
        || type == APIErrorType::NETWORK_ERROR;
}

// Retry helper: calls fn up to maxRetries times with exponential backoff
// fn should return true on success, throw APIError on failure
template<typename Fn>
void withRetry(Fn fn, int maxRetries = 3) {
    int attempt = 0;
    while (true) {
        try {
            fn();
            return; // Success
        } catch (const APIError& e) {
            attempt++;
            if (!isRetryable(e.type) || attempt > maxRetries) {
                throw; // Non-retryable or exhausted retries
            }
            // Exponential backoff: 1s, 2s, 4s
            int delayMs = 1000 * (1 << (attempt - 1));
            if (e.type == APIErrorType::RATE_LIMIT) {
                delayMs = std::max(delayMs, 2000); // Rate limit: at least 2s
            }
            spdlog::warn("{} (attempt {}/{}), retrying in {}ms...",
                         e.what(), attempt, maxRetries, delayMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }
}

} // namespace closecrab
