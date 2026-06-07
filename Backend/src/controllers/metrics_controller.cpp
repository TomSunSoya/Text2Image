#include "controllers/metrics_controller.h"

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "controllers/handler_utils.h"
#include "services/metrics_registry.h"
#include "utils/jwt_utils.h"

namespace {
void fillError(const drogon::HttpResponsePtr& resp, drogon::HttpStatusCode status,
               std::string_view message) {
    resp->setStatusCode(status);
    resp->setBody(nlohmann::json{{"error", std::string{message}}}.dump());
}

std::shared_ptr<cache::CacheMetrics>& metricsRef() {
    static std::shared_ptr<cache::CacheMetrics> instance;
    return instance;
}
} // namespace

void MetricsController::setMetrics(std::shared_ptr<cache::CacheMetrics> metrics) {
    metricsRef() = std::move(metrics);
}

void MetricsController::getCacheMetrics(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::ContentType::CT_APPLICATION_JSON);

    const auto token = utils::extractBearerToken(req);
    if (!token) {
        fillError(resp, drogon::HttpStatusCode::k401Unauthorized, "missing bearer token");
        controllers::auditRequest(req, "metrics.cache.read", "failure", std::nullopt,
                                  static_cast<int>(resp->getStatusCode()));
        callback(resp);
        return;
    }

    const auto payload = utils::verifyToken(*token);
    if (!payload || payload->user_id <= 0) {
        fillError(resp, drogon::HttpStatusCode::k401Unauthorized, "invalid token");
        controllers::auditRequest(req, "metrics.cache.read", "failure", std::nullopt,
                                  static_cast<int>(resp->getStatusCode()));
        callback(resp);
        return;
    }

    if (payload->role != "admin") {
        fillError(resp, drogon::HttpStatusCode::k403Forbidden, "admin role required");
        controllers::auditRequest(req, "metrics.cache.read", "failure",
                                  std::optional<int64_t>{payload->user_id},
                                  static_cast<int>(resp->getStatusCode()), "metrics:cache");
        callback(resp);
        return;
    }

    try {
        if (auto metrics = metricsRef()) {
            resp->setBody(metrics->toJson().dump());
        } else {
            resp->setStatusCode(drogon::HttpStatusCode::k503ServiceUnavailable);
            resp->setBody(R"({"error": "Cache metrics not initialized"})");
        }
    } catch (const std::exception& e) {
        spdlog::error("MetricsController::getCacheMetrics: error: {}", e.what());
        resp->setStatusCode(drogon::HttpStatusCode::k500InternalServerError);
        resp->setBody(R"({"error": "failed to render cache metrics"})");
    }
    controllers::auditRequest(req, "metrics.cache.read",
                              resp->getStatusCode() == drogon::HttpStatusCode::k200OK ? "success"
                                                                                      : "failure",
                              std::optional<int64_t>{payload->user_id},
                              static_cast<int>(resp->getStatusCode()), "metrics:cache");
    callback(resp);
}

void MetricsController::getPrometheusMetrics(
    const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->setContentTypeString("text/plain; version=0.0.4; charset=utf-8");
    resp->setBody(metrics::MetricsRegistry::instance().renderPrometheus());
    callback(resp);
}
