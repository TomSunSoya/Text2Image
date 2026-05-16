#include "controllers/metrics_controller.h"

#include <nlohmann/json.hpp>

namespace {
std::shared_ptr<cache::CacheMetrics>& metricsRef() {
    static std::shared_ptr<cache::CacheMetrics> instance;
    return instance;
}
} // namespace

void MetricsController::setMetrics(std::shared_ptr<cache::CacheMetrics> metrics) {
    metricsRef() = std::move(metrics);
}

void MetricsController::getCacheMetrics(
    const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::ContentType::CT_APPLICATION_JSON);

    if (auto metrics = metricsRef()) {
        resp->setBody(metrics->toJson().dump());
    } else {
        resp->setStatusCode(drogon::HttpStatusCode::k503ServiceUnavailable);
        resp->setBody(R"({"error": "Cache metrics not initialized"})");
    }
    callback(resp);
}
