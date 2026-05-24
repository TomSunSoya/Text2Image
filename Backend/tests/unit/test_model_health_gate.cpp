#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <utility>

#include "models/failure_code.h"
#include "services/model_health_gate.h"

namespace {

ImageHealthResult health(std::string status) {
    ImageHealthResult result;
    result.status = std::move(status);
    result.model_loaded = true;
    result.active_kind = "none";
    return result;
}

} // namespace

TEST(ModelHealthGate, HealthyProceeds) {
    const auto decision = model_health::decideForGenerate(health("healthy"), 0);

    EXPECT_EQ(decision.action, model_health::GateAction::Proceed);
}

TEST(ModelHealthGate, LoadingDefersWithLongBackoff) {
    auto loading = health("loading");
    loading.model_loaded = false;

    const auto decision = model_health::decideForGenerate(loading, 0);

    EXPECT_EQ(decision.action, model_health::GateAction::Defer);
    EXPECT_EQ(decision.backoff, std::chrono::seconds(30));
    EXPECT_EQ(decision.failure_code, std::string(models::failure::kModelServiceUnavailable));
}

TEST(ModelHealthGate, UnhealthyDefersWithShortBackoff) {
    auto unhealthy = health("unhealthy");
    unhealthy.detail = "connection refused";

    const auto decision = model_health::decideForGenerate(unhealthy, 0);

    EXPECT_EQ(decision.action, model_health::GateAction::Defer);
    EXPECT_EQ(decision.backoff, std::chrono::seconds(5));
    EXPECT_EQ(decision.message, "model service is unhealthy: connection refused");
}

TEST(ModelHealthGate, BusyAtCapacityDefers) {
    auto busy = health("busy");
    busy.active_kind = "generate";
    busy.active_generations = 1;
    busy.max_concurrent_generations = 1;

    const auto decision = model_health::decideForGenerate(busy, 0);

    EXPECT_EQ(decision.action, model_health::GateAction::Defer);
    EXPECT_EQ(decision.backoff, std::chrono::seconds(5));
}

TEST(ModelHealthGate, BusyBelowCapacityProceeds) {
    auto busy = health("busy");
    busy.active_kind = "generate";
    busy.active_generations = 1;
    busy.max_concurrent_generations = 2;

    const auto decision = model_health::decideForGenerate(busy, 0);

    EXPECT_EQ(decision.action, model_health::GateAction::Proceed);
}

TEST(ModelHealthGate, ExhaustedAttemptBudgetFails) {
    const auto decision = model_health::decideForGenerate(health("unhealthy"), 9);

    EXPECT_EQ(decision.action, model_health::GateAction::Fail);
    EXPECT_EQ(decision.failure_code, std::string(models::failure::kModelServiceUnavailable));
}
