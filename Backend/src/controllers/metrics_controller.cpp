#include "controllers/metrics_controller.h"

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

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
        callback(resp);
        return;
    }

    const auto payload = utils::verifyToken(*token);
    if (!payload || payload->user_id <= 0) {
        fillError(resp, drogon::HttpStatusCode::k401Unauthorized, "invalid token");
        callback(resp);
        return;
    }

    if (payload->role != "admin") {
        fillError(resp, drogon::HttpStatusCode::k403Forbidden, "admin role required");
        callback(resp);
        return;
    }

    if (auto metrics = metricsRef()) {
        resp->setBody(metrics->toJson().dump());
    } else {
        resp->setStatusCode(drogon::HttpStatusCode::k503ServiceUnavailable);
        resp->setBody(R"({"error": "Cache metrics not initialized"})");
    }
    callback(resp);
}
