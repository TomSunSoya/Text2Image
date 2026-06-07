#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "models/task_status.h"

namespace metrics {

class MetricsRegistry {
  public:
    static MetricsRegistry& instance();

    void resetForTests();

    void observeRequest(std::string_view endpoint, int statusCode, double seconds);
    void observeModelServiceCall(std::string_view endpoint, std::string_view status,
                                 double seconds);
    void recordTaskStatus(models::TaskStatus status);
    void incrementWorkerQueueDepth(int64_t delta = 1);
    void decrementWorkerQueueDepth(int64_t delta = 1);
    void setWorkerQueueDepth(int64_t depth);
    void setDbPoolStats(int64_t activeConnections, int64_t idleConnections);
    void setDbPoolCapacity(int64_t configuredConnections);

    [[nodiscard]] std::string renderPrometheus() const;

  private:
    MetricsRegistry() = default;

    struct Histogram {
        std::vector<uint64_t> buckets;
        uint64_t count{0};
        double sum{0.0};
    };

    void observeHistogram(std::map<std::string, Histogram>& histograms, std::string key,
                          double seconds);

    mutable std::mutex mutex_;
    std::map<std::string, Histogram> request_durations_;
    std::map<std::string, Histogram> model_service_durations_;
    std::map<std::string, uint64_t> task_totals_;
    int64_t worker_queue_depth_{0};
    int64_t db_pool_active_connections_{0};
    int64_t db_pool_idle_connections_{0};
    int64_t db_pool_configured_connections_{0};
};

[[nodiscard]] std::string requestLabels(std::string_view endpoint, int statusCode);
[[nodiscard]] std::string modelServiceLabels(std::string_view endpoint, std::string_view status);

} // namespace metrics
