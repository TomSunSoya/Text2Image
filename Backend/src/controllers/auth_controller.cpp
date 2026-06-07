#include "controllers/auth_controller.h"

#include <expected>
#include <format>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "controllers/handler_utils.h"
#include "services/auth_service.h"
#include "services/rate_limiter.h"

namespace {

int statusCode(const ServiceError& error) {
    return static_cast<int>(error.status);
}

std::expected<void, ServiceError> acquireAuthIpToken(const drogon::HttpRequestPtr& req) {
    const auto& rateConfig = rate_limit::defaultRateLimitConfig();
    if (!rateConfig.enabled) {
        return {};
    }

    const auto ip = controllers::clientIp(req, rateConfig.trust_proxy);
    return rate_limit::defaultRateLimiter()->tryAcquire(
        rate_limit::ipKey(ip, rateConfig), rateConfig.auth_ip_capacity, rateConfig.auth_ip_window);
}

nlohmann::json tokenResponse(const LoginResult& r) {
    return {{"token", r.access_token},
            {"access_token", r.access_token},
            {"refresh_token", r.refresh_token},
            {"expires_in", r.expires_in},
            {"user", r.user.toJson()}};
}

nlohmann::json tokenResponse(const RefreshResult& r) {
    return {{"token", r.access_token},
            {"access_token", r.access_token},
            {"refresh_token", r.refresh_token},
            {"expires_in", r.expires_in},
            {"user", r.user.toJson()}};
}

} // namespace

void AuthController::registerUser(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "AuthController::registerUser",
        [&req](const drogon::HttpResponsePtr& resp) {
            if (const auto acquired = acquireAuthIpToken(req); !acquired) {
                controllers::auditRequest(req, "auth.register", "failure", std::nullopt,
                                          statusCode(acquired.error()));
                controllers::fillServiceError(resp, acquired.error());
                return;
            }

            const auto payload = nlohmann::json::parse(req->getBody());
            AuthService service;
            const auto result = service.registerUser(payload);
            controllers::auditRequest(
                req, "auth.register", result ? "success" : "failure",
                result ? std::optional<int64_t>{result->user.id} : std::nullopt,
                result ? static_cast<int>(drogon::k200OK) : statusCode(result.error()),
                result ? std::format("user:{}", result->user.id) : std::string{});
            controllers::respondFromExpected(
                resp, result, drogon::k200OK,
                [](const RegisterResult& r) { return r.user.toJson().dump(); });
        });
}

void AuthController::login(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "AuthController::login", [&req](const drogon::HttpResponsePtr& resp) {
            if (const auto acquired = acquireAuthIpToken(req); !acquired) {
                controllers::auditRequest(req, "auth.login", "failure", std::nullopt,
                                          statusCode(acquired.error()));
                controllers::fillServiceError(resp, acquired.error());
                return;
            }

            const auto payload = nlohmann::json::parse(req->getBody());
            AuthService service;
            const auto result = service.login(payload);
            controllers::auditRequest(
                req, "auth.login", result ? "success" : "failure",
                result ? std::optional<int64_t>{result->user.id} : std::nullopt,
                result ? static_cast<int>(drogon::k200OK) : statusCode(result.error()),
                result ? std::format("user:{}", result->user.id) : std::string{});
            controllers::respondFromExpected(
                resp, result, drogon::k200OK,
                [](const LoginResult& r) { return tokenResponse(r).dump(); });
        });
}

void AuthController::refresh(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "AuthController::refresh",
        [&req](const drogon::HttpResponsePtr& resp) {
            const auto payload = nlohmann::json::parse(req->getBody());
            AuthService service;
            const auto result = service.refresh(payload);
            controllers::auditRequest(
                req, "auth.refresh", result ? "success" : "failure",
                result ? std::optional<int64_t>{result->user.id} : std::nullopt,
                result ? static_cast<int>(drogon::k200OK) : statusCode(result.error()),
                result ? std::format("user:{}", result->user.id) : std::string{});
            controllers::respondFromExpected(
                resp, result, drogon::k200OK,
                [](const RefreshResult& r) { return tokenResponse(r).dump(); });
        });
}

void AuthController::logout(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "AuthController::logout", [&req](const drogon::HttpResponsePtr& resp) {
            const auto payload = nlohmann::json::parse(req->getBody());
            AuthService service;
            const auto result = service.logout(payload);
            controllers::auditRequest(
                req, "auth.logout", result ? "success" : "failure", std::nullopt,
                result ? static_cast<int>(drogon::k200OK) : statusCode(result.error()));
            controllers::respondFromExpected(resp, result, drogon::k200OK,
                                             [] { return R"({"status":"ok"})"; });
        });
}

void AuthController::me(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "AuthController::me", [&req](const drogon::HttpResponsePtr& resp) {
            const auto userId = controllers::resolveUserId(req, resp);
            if (!userId) {
                controllers::auditRequest(req, "auth.me", "failure", std::nullopt,
                                          static_cast<int>(resp->getStatusCode()));
                return;
            }

            AuthService service;
            const auto result = service.getProfile(*userId);
            controllers::auditRequest(
                req, "auth.me", result ? "success" : "failure", std::optional<int64_t>{*userId},
                result ? static_cast<int>(drogon::k200OK) : statusCode(result.error()),
                std::format("user:{}", *userId));
            controllers::respondFromExpected(
                resp, result, drogon::k200OK,
                [](const models::User& user) { return user.toJson().dump(); });
        });
}

void AuthController::changePassword(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "AuthController::changePassword",
        [&req](const drogon::HttpResponsePtr& resp) {
            const auto userId = controllers::resolveUserId(req, resp);
            if (!userId) {
                controllers::auditRequest(req, "auth.password_change", "failure", std::nullopt,
                                          static_cast<int>(resp->getStatusCode()));
                return;
            }

            const auto payload = nlohmann::json::parse(req->getBody());
            AuthService service;
            const auto result = service.changePassword(*userId, payload);
            controllers::auditRequest(req, "auth.password_change", result ? "success" : "failure",
                                      std::optional<int64_t>{*userId},
                                      result ? static_cast<int>(drogon::k200OK)
                                             : statusCode(result.error()),
                                      std::format("user:{}", *userId));
            controllers::respondFromExpected(resp, result, drogon::k200OK,
                                             [] { return R"({"status":"ok"})"; });
        });
}
