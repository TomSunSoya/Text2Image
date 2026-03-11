#include "image_controller.h"

#include <optional>

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "image_service.h"
#include "jwt_utils.h"

namespace {

int parsePositiveInt(const std::string& value, int fallback)
{
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

nlohmann::json toListJson(const ImageListResult& result)
{
    nlohmann::json content = nlohmann::json::array();
    for (const auto& item : result.content) {
        auto summary = item.toJson();
        summary.erase("imageBase64");
        content.push_back(summary);
    }

    return {
        {"content", content},
        {"totalElements", result.total_elements}
    };
}

nlohmann::json toStatusJson(const models::ImageGeneration& generation)
{
    const auto full = generation.toJson();

    nlohmann::json body = {
        {"id", full.at("id")},
        {"requestId", full.at("requestId")},
        {"status", full.at("status")}
    };

    if (full.contains("errorMessage") && full.at("errorMessage").is_string() && !full.at("errorMessage").get<std::string>().empty()) {
        body["errorMessage"] = full.at("errorMessage");
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

    if (full.contains("imageUrl") && full.at("imageUrl").is_string() && !full.at("imageUrl").get<std::string>().empty()) {
        body["imageUrl"] = full.at("imageUrl");
    }

    if (full.contains("imageBase64") && full.at("imageBase64").is_string() && !full.at("imageBase64").get<std::string>().empty()) {
        body["imageBase64"] = full.at("imageBase64");
    }

    return body;
}

void fillServiceError(const drogon::HttpResponsePtr& resp, const ServiceError& error)
{
    resp->setStatusCode(error.status);
    resp->setBody(nlohmann::json{{"error", error.message}}.dump());
}

std::optional<int64_t> resolveUserId(const drogon::HttpRequestPtr& req,
                                     const drogon::HttpResponsePtr& resp)
{
    const auto token = utils::extractBearerToken(req);
    if (!token) {
        resp->setStatusCode(drogon::k401Unauthorized);
        resp->setBody(R"({"error":"missing bearer token"})");
        return std::nullopt;
    }

    const auto payload = utils::verifyToken(*token);
    if (!payload || payload->user_id <= 0) {
        resp->setStatusCode(drogon::k401Unauthorized);
        resp->setBody(R"({"error":"invalid token"})");
        return std::nullopt;
    }

    return payload->user_id;
}

} // namespace

void ImageController::checkHealth(const drogon::HttpRequestPtr&,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    ImageService service;
    const auto health = service.checkHealth();

    nlohmann::json body = {
        {"status", health.status},
        {"modelLoaded", health.model_loaded}
    };
    if (!health.detail.empty()) {
        body["detail"] = health.detail;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(body.dump());
    callback(resp);
}

void ImageController::create(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
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
        ServiceError error;
        auto result = service.create(*userId, payload, error);
        if (!result) {
            fillServiceError(resp, error);
            callback(resp);
            return;
        }

        resp->setStatusCode(drogon::k202Accepted);
        resp->setBody(result->generation.toJson().dump());
        callback(resp);
    } catch (const json::parse_error& e) {
        spdlog::error("ImageController::create parse error: {}", e.what());
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody(R"({"error":"invalid json body"})");
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("ImageController::create error: {}", e.what());
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody(R"({"error":"internal error"})");
        callback(resp);
    }
}

void ImageController::listMy(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
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
    ServiceError error;
    auto result = service.listMy(*userId, page, size, error);
    if (!result) {
        fillServiceError(resp, error);
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(toListJson(*result).dump());
    callback(resp);
}

void ImageController::listMyByStatus(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     const std::string& status)
{
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
    ServiceError error;
    auto result = service.listMyByStatus(*userId, status, page, size, error);
    if (!result) {
        fillServiceError(resp, error);
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(toListJson(*result).dump());
    callback(resp);
}

void ImageController::getById(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              int64_t id)
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    ImageService service;
    ServiceError error;
    auto result = service.getById(*userId, id, error);
    if (!result) {
        fillServiceError(resp, error);
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(result->generation.toJson().dump());
    callback(resp);
}

void ImageController::getStatusById(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    int64_t id)
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    ImageService service;
    ServiceError error;
    auto result = service.getById(*userId, id, error);
    if (!result) {
        fillServiceError(resp, error);
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(toStatusJson(result->generation).dump());
    callback(resp);
}

void ImageController::deleteById(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 int64_t id)
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    const auto userId = resolveUserId(req, resp);
    if (!userId) {
        callback(resp);
        return;
    }

    ImageService service;
    ServiceError error;
    if (!service.deleteById(*userId, id, error)) {
        fillServiceError(resp, error);
        callback(resp);
        return;
    }

    resp->setStatusCode(drogon::k200OK);
    resp->setBody(R"({"deleted":true})");
    callback(resp);
}


