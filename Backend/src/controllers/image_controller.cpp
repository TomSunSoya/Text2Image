#include "image_controller.h"

#include <optional>

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "image_service.h"
#include "jwt_utils.h"

namespace {

int parsePositiveInt(const std::string& value, int fallback) {
    if (value.empty()) {
        return fallback;
    }

    try {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

nlohmann::json toListJson(const ImageListResult& result) {
    nlohmann::json content = nlohmann::json::array();
    for (const auto& item : result.content) {
        content.push_back(item.toJson());
    }

    return {{"content", content}, {"totalElements", result.total_elements}};
}

nlohmann::json toStatusJson(const models::ImageGeneration& generation) {
    const auto full = generation.toJson();

    nlohmann::json body = {
        {"id", full.at("id")}, {"requestId", full.at("requestId")}, {"status", full.at("status")}};

    if (full.contains("errorMessage") && full.at("errorMessage").is_string() &&
        !full.at("errorMessage").get<std::string>().empty()) {
        body["errorMessage"] = full.at("errorMessage");
    }

    if (full.contains("failureCode") && full.at("failureCode").is_string() &&
        !full.at("failureCode").get<std::string>().empty()) {
        body["failureCode"] = full.at("failureCode");
    }

    if (full.contains("generationTime")) {
        body["generationTime"] = full.at("generationTime");
    }

    if (full.contains("createdAt")) {
        body["createdAt"] = full.at("createdAt");
    }

    if (full.contains("completedAt")) {
        body["completedAt"] = full.at("completedAt");
    }

    if (full.contains("imageUrl") && full.at("imageUrl").is_string() &&
        !full.at("imageUrl").get<std::string>().empty()) {
        body["imageUrl"] = full.at("imageUrl");
    }

    return body;
}

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
    // Fast path: JwtMiddleware already verified the token and stored userId
    // in request attributes — no need to re-parse the JWT.
    const auto& attrs = req->attributes();
    if (attrs) {
        try {
            auto userId = attrs->get<int64_t>("userId");
            if (userId > 0) {
                return userId;
            }
        } catch (...) {
            // Attribute not set (route has no JwtMiddleware filter); fall through.
        }
    }

    // Fallback: parse token ourselves (for unfiltered routes).
    const auto token = utils::extractBearerToken(req);
    if (!token) {
        fillDirectError(resp, drogon::k401Unauthorized, "missing_bearer_token",
                        "missing bearer token");
        return std::nullopt;
    }

    const auto payload = utils::verifyToken(*token);
    if (!payload || payload->user_id <= 0) {
        fillDirectError(resp, drogon::k401Unauthorized, "invalid_token", "invalid token");
        return std::nullopt;
    }

    return payload->user_id;
}

} // namespace

void ImageController::checkHealth(const drogon::HttpRequestPtr&,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    ImageService service;
    const auto health = service.checkHealth();

    nlohmann::json body = {{"status", health.status}, {"modelLoaded", health.model_loaded}};
    if (!health.detail.empty()) {
        body["detail"] = health.detail;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(body.dump());
    callback(resp);
}

void ImageController::create(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    using nlohmann::json;

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    try {
        const auto payload = json::parse(req->getBody());

        ImageService service;
        auto result = service.create(*userId, payload);
        if (!result) {
            fillServiceError(resp, result.error());
            callback(resp);
            return;
        }

        resp->setStatusCode(drogon::k202Accepted);
        resp->setBody(result->generation.toJson().dump());
        callback(resp);
    } catch (const json::parse_error& e) {
        spdlog::error("ImageController::create parse error: {}", e.what());
        fillDirectError(resp, drogon::k400BadRequest, "invalid_json_body", "invalid json body");
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("ImageController::create error: {}", e.what());
        fillDirectError(resp, drogon::k500InternalServerError, "internal_error", "internal error");
        callback(resp);
    }
}

void ImageController::listMy(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    const int page = parsePositiveInt(req->getParameter("page"), 0);
    const int size = parsePositiveInt(req->getParameter("size"), 10);

    ImageService service;
    auto result = service.listMy(*userId, page, size);
    if (!result) {
        fillServiceError(resp, result.error());
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(toListJson(*result).dump());
    callback(resp);
}

void ImageController::listMyByStatus(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     const std::string& status) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    const int page = parsePositiveInt(req->getParameter("page"), 0);
    const int size = parsePositiveInt(req->getParameter("size"), 10);

    ImageService service;
    auto result = service.listMyByStatus(*userId, status, page, size);
    if (!result) {
        fillServiceError(resp, result.error());
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(toListJson(*result).dump());
    callback(resp);
}

void ImageController::getById(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              int64_t id) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    ImageService service;
    auto result = service.getById(*userId, id, true);
    if (!result) {
        fillServiceError(resp, result.error());
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(result->generation.toJson().dump());
    callback(resp);
}

void ImageController::getBinaryById(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    int64_t id) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    ImageService service;
    auto result = service.getBinaryById(*userId, id);
    if (!result) {
        fillServiceError(resp, result.error());
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setContentTypeString(result->content_type);
    resp->setBody(result->body);
    callback(resp);
}

void ImageController::getStatusById(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    int64_t id) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    ImageService service;
    auto result = service.getById(*userId, id, false);
    if (!result) {
        fillServiceError(resp, result.error());
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(toStatusJson(result->generation).dump());
    callback(resp);
}

void ImageController::deleteById(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 int64_t id) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    ImageService service;
    auto result = service.deleteById(*userId, id);
    if (!result) {
        fillServiceError(resp, result.error());
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(R"({"deleted":true})");
    callback(resp);
}

void ImageController::cancelById(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 int64_t id) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    ImageService service;
    auto result = service.cancelById(*userId, id);
    if (!result) {
        fillServiceError(resp, result.error());
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(result->generation.toJson().dump());
    callback(resp);
}

void ImageController::retryById(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                int64_t id) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    ImageService service;
    auto result = service.retryById(*userId, id);
    if (!result) {
        fillServiceError(resp, result.error());
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(result->generation.toJson().dump());
    callback(resp);
}
