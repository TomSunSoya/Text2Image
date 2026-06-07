#include "controllers/image_controller.h"

#include <charconv>
#include <format>
#include <ranges>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "controllers/handler_utils.h"
#include "services/image_service.h"
#include "services/rate_limiter.h"

namespace {

int parsePositiveInt(const std::string& value, int fallback) {
    int parsed{};
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return ec == std::errc{} && parsed > 0 ? parsed : fallback;
}

nlohmann::json toListJson(const ImageListResult& result) {
    auto content = result.content | std::views::transform(&models::ImageGeneration::toJson);

    return {{"content", nlohmann::json(std::vector(content.begin(), content.end()))},
            {"totalElements", result.total_elements}};
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

} // namespace

void ImageController::checkHealth(const drogon::HttpRequestPtr&,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "ImageController::checkHealth",
        [](const drogon::HttpResponsePtr& resp) {
            ImageService service;
            const auto health = service.checkHealth();

            nlohmann::json body = {{"status", health.status},
                                   {"modelLoaded", health.model_loaded},
                                   {"activeKind", health.active_kind},
                                   {"activeGenerations", health.active_generations},
                                   {"maxConcurrentGenerations", health.max_concurrent_generations}};
            if (!health.detail.empty()) {
                body["detail"] = health.detail;
            }

            resp->setStatusCode(drogon::k200OK);
            resp->setBody(body.dump());
        });
}

void ImageController::create(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runJsonHandler(
        std::move(callback), "ImageController::create",
        [&req](const drogon::HttpResponsePtr& resp) {
            const auto user = controllers::resolveUser(req, resp);
            if (!user) {
                return;
            }

            const bool isAdmin = user->role == "admin";
            const auto& rateConfig = rate_limit::defaultRateLimitConfig();
            if (rateConfig.enabled && !isAdmin) {
                const auto acquired = rate_limit::defaultRateLimiter()->tryAcquire(
                    rate_limit::userKey(user->user_id, rateConfig), rateConfig.user_create_capacity,
                    rateConfig.user_create_window);
                if (!acquired) {
                    controllers::fillServiceError(resp, acquired.error());
                    return;
                }
            }

            const auto payload = nlohmann::json::parse(req->getBody());
            ImageService service;
            controllers::respondFromExpected(
                resp, service.create(user->user_id, payload, isAdmin), drogon::k202Accepted,
                [](const ImageCreateResult& r) { return r.generation.toJson().dump(); });
        });
}

void ImageController::listMy(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    controllers::runAuthenticatedJson(
        req, std::move(callback), "ImageController::listMy",
        [&req](int64_t userId, const drogon::HttpResponsePtr& resp) {
            const int page = parsePositiveInt(req->getParameter("page"), 0);
            const int size = parsePositiveInt(req->getParameter("size"), 10);
            ImageService service;
            controllers::respondFromExpected(
                resp, service.listMy(userId, page, size), drogon::k200OK,
                [](const ImageListResult& r) { return toListJson(r).dump(); });
        });
}

void ImageController::listMyByStatus(const drogon::HttpRequestPtr& req,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                     const std::string& status) {
    controllers::runAuthenticatedJson(
        req, std::move(callback), "ImageController::listMyByStatus",
        [&req, &status](int64_t userId, const drogon::HttpResponsePtr& resp) {
            const int page = parsePositiveInt(req->getParameter("page"), 0);
            const int size = parsePositiveInt(req->getParameter("size"), 10);
            ImageService service;
            controllers::respondFromExpected(
                resp, service.listMyByStatus(userId, status, page, size), drogon::k200OK,
                [](const ImageListResult& r) { return toListJson(r).dump(); });
        });
}

void ImageController::getById(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                              int64_t id) {
    controllers::runAuthenticatedJson(
        req, std::move(callback), "ImageController::getById",
        [id](int64_t userId, const drogon::HttpResponsePtr& resp) {
            ImageService service;
            controllers::respondFromExpected(
                resp, service.getById(userId, id, true), drogon::k200OK,
                [](const ImageGetResult& r) { return r.generation.toJson().dump(); });
        });
}

void ImageController::getBinaryById(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    int64_t id) {
    controllers::runAuthenticatedJson(req, std::move(callback), "ImageController::getBinaryById",
                                      [id](int64_t userId, const drogon::HttpResponsePtr& resp) {
                                          ImageService service;
                                          auto result = service.getBinaryById(userId, id);
                                          if (!result) {
                                              controllers::fillServiceError(resp, result.error());
                                              return;
                                          }

                                          resp->setStatusCode(drogon::k200OK);
                                          resp->setContentTypeString(result->content_type);
                                          resp->setBody(result->body);
                                      });
}

void ImageController::getStatusById(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                    int64_t id) {
    controllers::runAuthenticatedJson(
        req, std::move(callback), "ImageController::getStatusById",
        [id](int64_t userId, const drogon::HttpResponsePtr& resp) {
            ImageService service;
            controllers::respondFromExpected(
                resp, service.getById(userId, id, false), drogon::k200OK,
                [](const ImageGetResult& r) { return toStatusJson(r.generation).dump(); });
        });
}

void ImageController::deleteById(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 int64_t id) {
    controllers::runAuthenticatedJson(req, std::move(callback), "ImageController::deleteById",
                                      [id](int64_t userId, const drogon::HttpResponsePtr& resp) {
                                          ImageService service;
                                          controllers::respondFromExpected(
                                              resp, service.deleteById(userId, id), drogon::k200OK,
                                              [] { return std::string(R"({"deleted":true})"); });
                                      });
}

void ImageController::cancelById(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 int64_t id) {
    controllers::runAuthenticatedJson(
        req, std::move(callback), "ImageController::cancelById",
        [id](int64_t userId, const drogon::HttpResponsePtr& resp) {
            ImageService service;
            controllers::respondFromExpected(
                resp, service.cancelById(userId, id), drogon::k200OK,
                [](const ImageGetResult& r) { return r.generation.toJson().dump(); });
        });
}

void ImageController::retryById(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                int64_t id) {
    controllers::runAuthenticatedJson(
        req, std::move(callback), "ImageController::retryById",
        [id](int64_t userId, const drogon::HttpResponsePtr& resp) {
            ImageService service;
            controllers::respondFromExpected(
                resp, service.retryById(userId, id), drogon::k200OK,
                [](const ImageGetResult& r) { return r.generation.toJson().dump(); });
        });
}
