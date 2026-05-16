#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <drogon/HttpRequest.h>

namespace utils {

struct JwtPayload {
    int64_t user_id{0};
    std::string username;
    std::string role{"user"};
};

std::string createToken(int64_t userId, const std::string& username,
                        const std::string& role = "user");
std::optional<JwtPayload> verifyToken(const std::string& token);
std::optional<std::string> extractBearerToken(const drogon::HttpRequestPtr& req);

} // namespace utils
