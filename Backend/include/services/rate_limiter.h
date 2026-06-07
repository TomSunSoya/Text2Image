#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

#include "services/i_rate_limiter.h"
#include "services/redis_client.h"

namespace rate_limit {

struct RateLimitConfig {
    bool enabled{true};
    bool fail_open{true};
    bool trust_proxy{false};
    int max_active_tasks_per_user{3};
    int user_create_capacity{10};
    std::chrono::seconds user_create_window{60};
    int auth_ip_capacity{5};
    std::chrono::seconds auth_ip_window{60};
    std::string key_prefix{"zimage:rate:"};
    redis::RedisConfig redis;
};

[[nodiscard]] RateLimitConfig parseRateLimitConfig(const nlohmann::json& rateLimitJson,
                                                   const redis::RedisConfig& redisConfig);
[[nodiscard]] RateLimitConfig loadRateLimitConfig(const nlohmann::json& backendConfig);
[[nodiscard]] RateLimitConfig loadRateLimitConfig();

[[nodiscard]] std::string userKey(int64_t userId, const RateLimitConfig& config);
[[nodiscard]] std::string ipKey(std::string_view ip, const RateLimitConfig& config);

class NullRateLimiter final : public IRateLimiter {
  public:
    [[nodiscard]] std::expected<void, ServiceError> tryAcquire(std::string_view, int,
                                                               std::chrono::seconds) override {
        return {};
    }
};

class RedisTokenBucketLimiter final : public IRateLimiter {
  public:
    explicit RedisTokenBucketLimiter(RateLimitConfig config);
    ~RedisTokenBucketLimiter() override;

    [[nodiscard]] bool isAvailable() const noexcept;

    [[nodiscard]] std::expected<void, ServiceError>
    tryAcquire(std::string_view key, int capacity, std::chrono::seconds window) override;

  private:
    struct Impl;

    [[nodiscard]] long long evalTokenBucket(std::string_view key, int capacity,
                                            std::chrono::seconds window);
    void loadScript();

    RateLimitConfig config_;
    std::unique_ptr<Impl> impl_;
    std::string script_sha_;
    mutable std::mutex script_mutex_;
    std::atomic_bool available_{false};
};

void configureDefaultRateLimiter(const nlohmann::json& backendConfig);
[[nodiscard]] const RateLimitConfig& defaultRateLimitConfig();
[[nodiscard]] std::shared_ptr<IRateLimiter> defaultRateLimiter();
void setDefaultRateLimiterForTesting(std::shared_ptr<IRateLimiter> limiter);

} // namespace rate_limit
