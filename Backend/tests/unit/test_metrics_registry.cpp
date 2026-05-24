#include "services/metrics_registry.h"

#include <gtest/gtest.h>

namespace {

class MetricsRegistryTest : public ::testing::Test {
  protected:
    void SetUp() override {
        metrics::MetricsRegistry::instance().resetForTests();
    }
};

TEST_F(MetricsRegistryTest, RendersRequestHistogramAndTaskCounter) {
    auto& registry = metrics::MetricsRegistry::instance();
    registry.observeRequest("ImageController::create", 202, 0.42);
    registry.recordTaskStatus(models::TaskStatus::Queued);

    const auto text = registry.renderPrometheus();

    EXPECT_NE(text.find("# TYPE image_request_duration_seconds histogram"), std::string::npos);
    EXPECT_NE(
        text.find("image_request_duration_seconds_bucket{endpoint=\"ImageController::create\","
                  "status=\"202\",le=\"0.5\"} 1"),
        std::string::npos);
    EXPECT_NE(text.find("image_task_total{status=\"queued\"} 1"), std::string::npos);
}

TEST_F(MetricsRegistryTest, TracksQueueAndDbGauges) {
    auto& registry = metrics::MetricsRegistry::instance();
    registry.incrementWorkerQueueDepth();
    registry.incrementWorkerQueueDepth(2);
    registry.decrementWorkerQueueDepth();
    registry.setDbPoolStats(3, 7);

    const auto text = registry.renderPrometheus();

    EXPECT_NE(text.find("worker_queue_depth 2"), std::string::npos);
    EXPECT_NE(text.find("db_pool_active_connections 3"), std::string::npos);
    EXPECT_NE(text.find("db_pool_idle 7"), std::string::npos);
}

TEST_F(MetricsRegistryTest, RendersModelServiceHistogram) {
    auto& registry = metrics::MetricsRegistry::instance();
    registry.observeModelServiceCall("/generate", "200", 6.0);

    const auto text = registry.renderPrometheus();

    EXPECT_NE(text.find("# TYPE model_service_call_duration_seconds histogram"), std::string::npos);
    EXPECT_NE(text.find("model_service_call_duration_seconds_bucket{endpoint=\"/generate\","
                        "status=\"200\",le=\"10\"} 1"),
              std::string::npos);
}

} // namespace
