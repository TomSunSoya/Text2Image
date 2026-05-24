#pragma once

#include <memory>

#include <drogon/HttpController.h>
#include "services/cache_metrics.h"

class MetricsController : public drogon::HttpController<MetricsController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MetricsController::getPrometheusMetrics, "/metrics", drogon::Get);
    ADD_METHOD_TO(MetricsController::getCacheMetrics, "/api/metrics/cache", drogon::Get);
    METHOD_LIST_END

    static void setMetrics(std::shared_ptr<cache::CacheMetrics> metrics);

    void getCacheMetrics(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void getPrometheusMetrics(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};
