#include "controllers/auth_controller.h"

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "services/auth_service.h"

static void fillServiceError(const ServiceError& error, drogon::HttpResponsePtr& resp) {
    using nlohmann::json;
    resp->setStatusCode(error.status);
    resp->setBody(json{{"error", error.toJson()}}.dump());
}

void AuthController::registerUser(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    using nlohmann::json;

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    try {
        const auto payload = json::parse(req->getBody());

        AuthService service;
        auto result = service.registerUser(payload);
        if (!result) {
            fillServiceError(result.error(), resp);
            callback(resp);
            return;
        }

        resp->setStatusCode(drogon::k200OK);
        resp->setBody(result->user.toJson().dump());
        callback(resp);
    } catch (const json::parse_error& e) {
        spdlog::error("registerUser parse error: {}", e.what());
        fillServiceError(
            ServiceError{drogon::k400BadRequest, "invalid_json_body", "invalid json body"}, resp);
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("registerUser error: {}", e.what());
        fillServiceError(
            ServiceError{drogon::k500InternalServerError, "internal_error", "internal error"},
            resp);
        callback(resp);
    }
}

void AuthController::login(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    using nlohmann::json;

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    try {
        const auto payload = json::parse(req->getBody());

        AuthService service;
        auto result = service.login(payload);
        if (!result) {
            fillServiceError(result.error(), resp);
            callback(resp);
            return;
        }

        json out{{"token", result->token}, {"user", result->user.toJson()}};

        resp->setStatusCode(drogon::k200OK);
        resp->setBody(out.dump());
        callback(resp);
    } catch (const json::parse_error& e) {
        spdlog::error("login parse error: {}", e.what());
        fillServiceError(
            ServiceError{drogon::k400BadRequest, "invalid_json_body", "invalid json body"}, resp);
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("login error: {}", e.what());
        fillServiceError(
            ServiceError{drogon::k500InternalServerError, "internal_error", "internal error"},
            resp);
        callback(resp);
    }
}
