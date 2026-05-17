#include "controllers/handler_utils.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "utils/jwt_utils.h"

namespace controllers {

void fillServiceError(const drogon::HttpResponsePtr& resp, const ServiceError& error) {
    resp->setStatusCode(error.status);
    resp->setBody(nlohmann::json{{"error", error.toJson()}}.dump());
}

void fillDirectError(const drogon::HttpResponsePtr& resp, drogon::HttpStatusCode status,
                     std::string code, std::string message) {
    fillServiceError(resp, ServiceError{status, std::move(code), std::move(message)});
}

std::optional<int64_t> resolveUserId(const drogon::HttpRequestPtr& req,
                                     const drogon::HttpResponsePtr& resp) {
    const auto& attrs = req->attributes();
    if (attrs) {
        try {
            auto userId = attrs->get<int64_t>("userId");
            if (userId > 0) {
                return userId;
            }
        } catch (...) {
            // Attribute not set; fall through to token parsing for unfiltered routes.
        }
    }

    bool tokenPresent = false;
    return utils::extractBearerToken(req)
        .and_then([&](const std::string& token) {
            tokenPresent = true;
            return utils::verifyToken(token);
        })
        .and_then([](const auto& payload) -> std::optional<int64_t> {
            return payload.user_id > 0 ? std::optional<int64_t>{payload.user_id} : std::nullopt;
        })
        .or_else([&]() -> std::optional<int64_t> {
            if (!tokenPresent) {
                fillDirectError(resp, drogon::k401Unauthorized, "missing_bearer_token",
                                "missing bearer token");
            } else {
                fillDirectError(resp, drogon::k401Unauthorized, "invalid_token", "invalid token");
            }
            return std::nullopt;
        });
}

} // namespace controllers
