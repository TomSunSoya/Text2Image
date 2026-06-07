#pragma once

#include <chrono>
#include <string>

#include "services/image_service_types.h"

namespace model_health {

enum class GateAction {
    Proceed,
    Defer,
    Fail,
};

struct GateDecision {
    GateAction action{GateAction::Proceed};
    std::chrono::seconds backoff{0};
    std::string failure_code;
    std::string message;
};

[[nodiscard]] GateDecision decideForGenerate(const ImageHealthResult& health, int retryCount);

} // namespace model_health
