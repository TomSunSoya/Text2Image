#include "services/model_health_gate.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <string>
#include <string_view>

#include "models/failure_code.h"

namespace {

constexpr int kMaxModelHealthAttempts = 10;
constexpr std::chrono::seconds kUnhealthyBackoff{5};
constexpr std::chrono::seconds kLoadingBackoff{30};

std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool hasExceededAttemptBudget(int retryCount) {
    return retryCount >= kMaxModelHealthAttempts - 1;
}

std::string healthMessage(const ImageHealthResult& health, std::string_view reason) {
    if (!health.detail.empty()) {
        return std::format("{}: {}", reason, health.detail);
    }
    return std::string(reason);
}

model_health::GateDecision deferOrFail(const ImageHealthResult& health, int retryCount,
                                       std::chrono::seconds backoff, std::string_view reason) {
    if (hasExceededAttemptBudget(retryCount)) {
        return {.action = model_health::GateAction::Fail,
                .backoff = std::chrono::seconds{0},
                .failure_code = std::string(models::failure::kModelServiceUnavailable),
                .message = healthMessage(health, "model service unavailable")};
    }

    return {.action = model_health::GateAction::Defer,
            .backoff = backoff,
            .failure_code = std::string(models::failure::kModelServiceUnavailable),
            .message = healthMessage(health, reason)};
}

bool busyGenerationConflicts(const ImageHealthResult& health) {
    const auto activeKind = lower(health.active_kind);
    if (activeKind.empty() || activeKind == "none") {
        return false;
    }

    const bool capacityKnown = health.max_concurrent_generations > 0;
    if (!capacityKnown) {
        return true;
    }

    return health.active_generations >= health.max_concurrent_generations;
}

} // namespace

namespace model_health {

GateDecision decideForGenerate(const ImageHealthResult& health, int retryCount) {
    const auto status = lower(health.status);

    if (status == "healthy" || status == "ok" || status == "success") {
        return {};
    }

    if (status == "loading") {
        return deferOrFail(health, retryCount, kLoadingBackoff, "model service is loading");
    }

    if (status == "busy") {
        if (!busyGenerationConflicts(health)) {
            return {};
        }
        return deferOrFail(health, retryCount, kUnhealthyBackoff, "model service is busy");
    }

    return deferOrFail(health, retryCount, kUnhealthyBackoff, "model service is unhealthy");
}

} // namespace model_health
