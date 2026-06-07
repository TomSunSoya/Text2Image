#include "utils/jwt_utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <random>
#include <string_view>

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

#include "Backend.h"

namespace {

using traits = jwt::traits::nlohmann_json;

constexpr int kDefaultAccessExpirationMinutes = 15;
constexpr int kDefaultRefreshExpirationDays = 7;

const std::string& loadSecret() {
    static const std::string secret = [] {
        const auto& config = backend::cachedConfig();
        auto value = config.at("jwt").at("secret").get<std::string>();
        if (value.empty()) {
            throw std::runtime_error(
                "jwt.secret is empty — set JWT_SECRET env var or update config.json");
        }
        return value;
    }();
    return secret;
}

std::string loadIssuer() {
    return "backend";
}

int positiveConfigInt(std::string_view key, int fallback) {
    try {
        const auto& jwtConfig = backend::cachedConfig().at("jwt");
        if (jwtConfig.contains(std::string{key}) &&
            jwtConfig.at(std::string{key}).is_number_integer()) {
            return (std::max)(1, jwtConfig.at(std::string{key}).get<int>());
        }
    } catch (...) {
    }
    return fallback;
}

int loadAccessExpirationMinutes() {
    return positiveConfigInt("access_expiration_minutes", kDefaultAccessExpirationMinutes);
}

std::chrono::system_clock::time_point expiresAfter(std::chrono::seconds ttl) {
    return std::chrono::system_clock::now() + ttl;
}

std::string claimString(const jwt::decoded_jwt<traits>& decoded, std::string_view name) {
    if (!decoded.has_payload_claim(std::string{name})) {
        return {};
    }
    return decoded.get_payload_claim(std::string{name}).as_string();
}

} // namespace

namespace utils {

std::string createToken(int64_t userId, const std::string& username, const std::string& role) {
    return jwt::create<jwt::default_clock, traits>(jwt::default_clock{})
        .set_issuer(loadIssuer())
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(expiresAfter(std::chrono::seconds(accessTokenExpiresInSeconds())))
        .set_payload_claim("uid", traits::value_type(static_cast<traits::integer_type>(userId)))
        .set_payload_claim("username", traits::value_type(username))
        .set_payload_claim("role", traits::value_type(role.empty() ? "user" : role))
        .set_payload_claim("type", traits::value_type("access"))
        .set_payload_claim("jti", traits::value_type(generateJti()))
        .sign(jwt::algorithm::hs256{loadSecret()});
}

std::optional<JwtPayload> verifyToken(const std::string& token) {
    try {
        auto decoded = jwt::decode<traits>(token);

        auto verifier = jwt::verify<jwt::default_clock, traits>(jwt::default_clock{})
                            .allow_algorithm(jwt::algorithm::hs256{loadSecret()})
                            .with_issuer(loadIssuer());

        verifier.verify(decoded);

        const auto tokenType = claimString(decoded, "type");
        if (!tokenType.empty() && tokenType != "access") {
            return std::nullopt;
        }

        const auto uidClaim = decoded.get_payload_claim("uid");

        JwtPayload payload;
        try {
            payload.user_id = static_cast<int64_t>(uidClaim.as_integer());
        } catch (...) {
            // Backward-compatible path for tokens that encode uid as float.
            payload.user_id = static_cast<int64_t>(uidClaim.as_number());
        }
        payload.username = decoded.get_payload_claim("username").as_string();
        if (decoded.has_payload_claim("role")) {
            payload.role = decoded.get_payload_claim("role").as_string();
            if (payload.role.empty()) {
                payload.role = "user";
            }
        }
        payload.jti = claimString(decoded, "jti");
        return payload;
    } catch (...) {
        return std::nullopt;
    }
}

std::string issueRefreshToken(int64_t userId, const std::string& jti) {
    return jwt::create<jwt::default_clock, traits>(jwt::default_clock{})
        .set_issuer(loadIssuer())
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(expiresAfter(std::chrono::seconds(refreshTokenExpiresInSeconds())))
        .set_payload_claim("uid", traits::value_type(static_cast<traits::integer_type>(userId)))
        .set_payload_claim("type", traits::value_type("refresh"))
        .set_payload_claim("jti", traits::value_type(jti))
        .sign(jwt::algorithm::hs256{loadSecret()});
}

std::expected<RefreshTokenClaims, ServiceError> verifyRefreshToken(const std::string& token) {
    try {
        auto decoded = jwt::decode<traits>(token);

        auto verifier = jwt::verify<jwt::default_clock, traits>(jwt::default_clock{})
                            .allow_algorithm(jwt::algorithm::hs256{loadSecret()})
                            .with_issuer(loadIssuer());

        verifier.verify(decoded);

        if (claimString(decoded, "type") != "refresh") {
            return std::unexpected(ServiceError{drogon::k401Unauthorized, "invalid_refresh_token",
                                                "invalid refresh token"});
        }

        const auto jti = claimString(decoded, "jti");
        if (jti.empty()) {
            return std::unexpected(ServiceError{drogon::k401Unauthorized, "invalid_refresh_token",
                                                "invalid refresh token"});
        }

        const auto uidClaim = decoded.get_payload_claim("uid");
        RefreshTokenClaims claims;
        try {
            claims.user_id = static_cast<int64_t>(uidClaim.as_integer());
        } catch (...) {
            claims.user_id = static_cast<int64_t>(uidClaim.as_number());
        }
        claims.jti = jti;
        return claims;
    } catch (...) {
        return std::unexpected(ServiceError{drogon::k401Unauthorized, "invalid_refresh_token",
                                            "invalid refresh token"});
    }
}

std::string generateJti() {
    std::array<unsigned char, 16> bytes{};
    std::random_device rd;
    for (auto& byte : bytes) {
        byte = static_cast<unsigned char>(rd());
    }

    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    return std::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-"
                       "{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                       bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
                       bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
                       bytes[14], bytes[15]);
}

int accessTokenExpiresInSeconds() {
    return loadAccessExpirationMinutes() * 60;
}

int refreshTokenExpiresInSeconds() {
    return positiveConfigInt("refresh_expiration_days", kDefaultRefreshExpirationDays) * 24 * 60 *
           60;
}

std::optional<std::string> extractBearerToken(const drogon::HttpRequestPtr& req) {
    const auto header = req->getHeader("Authorization");
    constexpr std::string_view prefix = "Bearer ";

    if (header.size() <= prefix.size()) {
        return std::nullopt;
    }

    if (!std::equal(prefix.begin(), prefix.end(), header.begin())) {
        return std::nullopt;
    }

    return header.substr(prefix.size());
}

} // namespace utils
