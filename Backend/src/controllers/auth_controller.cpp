#include "auth_controller.h"

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "auth_service.h"

void AuthController::registerUser(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    using nlohmann::json;

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    try {
        const auto payload = json::parse(req->getBody());

        AuthService service;
        ServiceError error;
        auto result = service.registerUser(payload, error);
        if (!result) {
            resp->setStatusCode(error.status);
            resp->setBody(json{{"error", error.message}}.dump());
            callback(resp);
            return;
        }

        resp->setStatusCode(drogon::k200OK);
        resp->setBody(result->user.toJson().dump());
        callback(resp);
    } catch (const json::parse_error& e) {
        spdlog::error("registerUser parse error: {}", e.what());
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody(R"({"error":"invalid json body"})");
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("registerUser error: {}", e.what());
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody(R"({"error":"internal error"})");
        callback(resp);
    }
}

void AuthController::login(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    using nlohmann::json;

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    try {
        const auto payload = json::parse(req->getBody());

        AuthService service;
        ServiceError error;
        auto result = service.login(payload, error);
        if (!result) {
            resp->setStatusCode(error.status);
            resp->setBody(json{{"error", error.message}}.dump());
            callback(resp);
            return;
        }

        json out{
            {"token", result->token},
            {"user", result->user.toJson()}
        };

        resp->setStatusCode(drogon::k200OK);
        resp->setBody(out.dump());
        callback(resp);
    } catch (const json::parse_error& e) {
        spdlog::error("login parse error: {}", e.what());
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody(R"({"error":"invalid json body"})");
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("login error: {}", e.what());
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody(R"({"error":"internal error"})");
        callback(resp);
    }
}
