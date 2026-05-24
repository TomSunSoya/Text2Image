#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include <drogon/HttpRequest.h>

#include "services/service_error.h"

namespace utils {

struct JwtPayload {
    int64_t user_id{0};
    std::string username;
    std::string role{"user"};
    std::string jti;
};

struct RefreshTokenClaims {
    int64_t user_id{0};
    std::string jti;
};

std::string createToken(int64_t userId, const std::string& username,
                        const std::string& role = "user");
std::optional<JwtPayload> verifyToken(const std::string& token);
std::string issueRefreshToken(int64_t userId, const std::string& jti);
std::expected<RefreshTokenClaims, ServiceError> verifyRefreshToken(const std::string& token);
std::string generateJti();
int accessTokenExpiresInSeconds();
int refreshTokenExpiresInSeconds();
std::optional<std::string> extractBearerToken(const drogon::HttpRequestPtr& req);

} // namespace utils
