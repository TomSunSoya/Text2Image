#include "controllers/auth_controller.h"

#include <nlohmann/json.hpp>

#include "controllers/handler_utils.h"
#include "services/auth_service.h"

void AuthController::registerUser(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "AuthController::registerUser",
        [&req](const drogon::HttpResponsePtr& resp) {
            const auto payload = nlohmann::json::parse(req->getBody());
            AuthService service;
            controllers::respondFromExpected(
                resp, service.registerUser(payload), drogon::k200OK,
                [](const RegisterResult& r) { return r.user.toJson().dump(); });
        });
}

void AuthController::login(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "AuthController::login", [&req](const drogon::HttpResponsePtr& resp) {
            const auto payload = nlohmann::json::parse(req->getBody());
            AuthService service;
            controllers::respondFromExpected(
                resp, service.login(payload), drogon::k200OK, [](const LoginResult& r) {
                    return nlohmann::json{{"token", r.token}, {"user", r.user.toJson()}}.dump();
                });
        });
}
