#pragma once

#include <chrono>
#include <expected>
#include <string_view>

#include "services/service_error.h"

class IRateLimiter {
  public:
    virtual ~IRateLimiter() = default;

    [[nodiscard]] virtual std::expected<void, ServiceError>
    tryAcquire(std::string_view key, int capacity, std::chrono::seconds window) = 0;
};
