#pragma once
#include <string>
#include <cstdint>
#include <ctime>
#include <nlohmann/json.hpp>

namespace closecrab {

class JWTUtils {
public:
    // Decode JWT payload (middle segment) without signature verification
    static nlohmann::json decodePayload(const std::string& jwt) {
        // JWT format: header.payload.signature
        auto dot1 = jwt.find('.');
        if (dot1 == std::string::npos) return {};
        auto dot2 = jwt.find('.', dot1 + 1);
        if (dot2 == std::string::npos) return {};

        std::string payload = jwt.substr(dot1 + 1, dot2 - dot1 - 1);
        std::string decoded = base64UrlDecode(payload);
        try {
            return nlohmann::json::parse(decoded);
        } catch (...) {
            return {};
        }
    }

    // Check if JWT is expired (with margin)
    static bool isExpired(const std::string& jwt, int marginSeconds = 60) {
        int64_t exp = getExpiry(jwt);
        if (exp <= 0) return true;
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        return now >= (exp - marginSeconds);
    }

    // Get expiry timestamp from JWT
    static int64_t getExpiry(const std::string& jwt) {
        auto payload = decodePayload(jwt);
        if (payload.contains("exp") && payload["exp"].is_number()) {
            return payload["exp"].get<int64_t>();
        }
        return 0;
    }

    // Get subject claim
    static std::string getSubject(const std::string& jwt) {
        auto payload = decodePayload(jwt);
        return payload.value("sub", "");
    }

private:
    static std::string base64UrlDecode(const std::string& input) {
        // Convert base64url to standard base64
        std::string b64 = input;
        for (char& c : b64) {
            if (c == '-') c = '+';
            else if (c == '_') c = '/';
        }
        // Add padding
        while (b64.size() % 4 != 0) b64 += '=';

        // Decode base64
        static const std::string chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        int val = 0, bits = -8;
        for (unsigned char c : b64) {
            if (c == '=') break;
            auto pos = chars.find(c);
            if (pos == std::string::npos) continue;
            val = (val << 6) + static_cast<int>(pos);
            bits += 6;
            if (bits >= 0) {
                result += static_cast<char>((val >> bits) & 0xFF);
                bits -= 8;
            }
        }
        return result;
    }
};

} // namespace closecrab
