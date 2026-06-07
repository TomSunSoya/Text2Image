#include "controllers/handler_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "services/rate_limiter.h"
#include "utils/audit_log.h"
#include "utils/jwt_utils.h"

namespace {

std::string trimWhitespace(std::string_view s) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    auto begin = std::find_if(s.begin(), s.end(), notSpace);
    auto end = std::find_if(s.rbegin(), std::make_reverse_iterator(begin), notSpace).base();
    return std::string(begin, end);
}

} // namespace

namespace controllers {

std::string clientIp(const drogon::HttpRequestPtr& req, bool trustProxy) {
    if (trustProxy) {
        if (const auto xff = req->getHeader("X-Forwarded-For"); !xff.empty()) {
            auto comma = xff.find(',');
            auto first = comma == std::string::npos ? xff : xff.substr(0, comma);
            auto ip = trimWhitespace(first);
            if (!ip.empty()) {
                return ip;
            }
        }
        if (const auto xri = req->getHeader("X-Real-IP"); !xri.empty()) {
            auto ip = trimWhitespace(xri);
            if (!ip.empty()) {
                return ip;
            }
        }
    }
    return req->peerAddr().toIp();
}

void auditRequest(const drogon::HttpRequestPtr& req, std::string_view event,
                  std::string_view outcome, std::optional<int64_t> userId, int statusCode,
                  std::string_view resourceId) {
    const auto& rateConfig = rate_limit::defaultRateLimitConfig();
    audit::logHttpEvent(req, event, outcome, userId, statusCode,
                        clientIp(req, rateConfig.trust_proxy), resourceId);
}

void fillServiceError(const drogon::HttpResponsePtr& resp, const ServiceError& error) {
    resp->setStatusCode(error.status);
    resp->setBody(nlohmann::json{{"error", error.toJson()}}.dump());
}

void fillDirectError(const drogon::HttpResponsePtr& resp, drogon::HttpStatusCode status,
                     std::string code, std::string message) {
    fillServiceError(resp, ServiceError{status, std::move(code), std::move(message)});
}

std::optional<utils::JwtPayload> resolveUser(const drogon::HttpRequestPtr& req,
                                             const drogon::HttpResponsePtr& resp) {
    const auto& attrs = req->attributes();
    if (attrs) {
        try {
            auto userId = attrs->get<int64_t>("userId");
            if (userId > 0) {
                utils::JwtPayload payload;
                payload.user_id = userId;
                try {
                    payload.role = attrs->get<std::string>("userRole");
                    if (payload.role.empty()) {
                        payload.role = "user";
                    }
                } catch (...) {
                    payload.role = "user";
                }
                return payload;
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
        .and_then([](const auto& payload) -> std::optional<utils::JwtPayload> {
            return payload.user_id > 0 ? std::optional<utils::JwtPayload>{payload} : std::nullopt;
        })
        .or_else([&]() -> std::optional<utils::JwtPayload> {
            if (!tokenPresent) {
                fillDirectError(resp, drogon::k401Unauthorized, "missing_bearer_token",
                                "missing bearer token");
            } else {
                fillDirectError(resp, drogon::k401Unauthorized, "invalid_token", "invalid token");
            }
            return std::nullopt;
        });
}

std::optional<int64_t> resolveUserId(const drogon::HttpRequestPtr& req,
                                     const drogon::HttpResponsePtr& resp) {
    return resolveUser(req, resp).and_then([](const auto& payload) -> std::optional<int64_t> {
        return payload.user_id > 0 ? std::optional<int64_t>{payload.user_id} : std::nullopt;
    });
}

} // namespace controllers
