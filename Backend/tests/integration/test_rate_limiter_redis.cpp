#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "services/rate_limiter.h"

namespace {

std::optional<std::string> readEnv(const char* name) {
#ifdef _WIN32
    char* raw = nullptr;
    size_t size = 0;
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
        return std::nullopt;
    }

    std::string value(raw);
    free(raw);
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return std::nullopt;
    }

    std::string value(raw);
#endif
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> firstEnv(std::initializer_list<const char*> names) {
    for (const auto* name : names) {
        if (auto value = readEnv(name)) {
            return value;
        }
    }
    return std::nullopt;
}

int readEnvInt(std::initializer_list<const char*> names, int fallback) {
    if (const auto value = firstEnv(names)) {
        try {
            return std::stoi(*value);
        } catch (...) {
        }
    }
    return fallback;
}

rate_limit::RateLimitConfig redisIntegrationConfig() {
    rate_limit::RateLimitConfig cfg;
    cfg.enabled = true;
    cfg.fail_open = false;
    cfg.redis.enabled = true;
    cfg.redis.host = firstEnv({"TEST_REDIS_HOST", "REDIS_HOST"}).value_or("127.0.0.1");
    cfg.redis.port = readEnvInt({"TEST_REDIS_PORT", "REDIS_PORT"}, 6379);
    cfg.redis.password =
        firstEnv({"TEST_REDIS_PASSWORD", "REDIS_PASSWORD"}).value_or(std::string{});
    cfg.redis.db = readEnvInt({"TEST_REDIS_DB"}, 15);
    cfg.redis.pool_size = 1;
    cfg.redis.connect_timeout_ms = 200;
    cfg.redis.socket_timeout_ms = 200;
    cfg.key_prefix = "zimage:test:rate:" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     ":";
    return cfg;
}

} // namespace

class RateLimiterRedisIntegration : public ::testing::Test {
  protected:
    void SetUp() override {
        config_ = redisIntegrationConfig();
        limiter_ = std::make_unique<rate_limit::RedisTokenBucketLimiter>(config_);
        if (!limiter_->isAvailable()) {
            GTEST_SKIP() << "Redis rate limiter integration skipped: " << config_.redis.host << ":"
                         << config_.redis.port
                         << " is unavailable. Set TEST_REDIS_* to run this test.";
        }
    }

    rate_limit::RateLimitConfig config_;
    std::unique_ptr<rate_limit::RedisTokenBucketLimiter> limiter_;
};

TEST_F(RateLimiterRedisIntegration, TokenBucketDeniesAfterCapacity) {
    const auto key = config_.key_prefix + "user:42";

    EXPECT_TRUE(limiter_->tryAcquire(key, 2, std::chrono::seconds(60)).has_value());
    EXPECT_TRUE(limiter_->tryAcquire(key, 2, std::chrono::seconds(60)).has_value());

    const auto denied = limiter_->tryAcquire(key, 2, std::chrono::seconds(60));
    ASSERT_FALSE(denied.has_value());
    EXPECT_EQ(denied.error().status, drogon::k429TooManyRequests);
    EXPECT_EQ(denied.error().code, "rate_limit_exceeded");
}

TEST_F(RateLimiterRedisIntegration, KeysAreIsolated) {
    const auto user42 = config_.key_prefix + "user:42";
    const auto user43 = config_.key_prefix + "user:43";

    EXPECT_TRUE(limiter_->tryAcquire(user42, 1, std::chrono::seconds(60)).has_value());
    EXPECT_FALSE(limiter_->tryAcquire(user42, 1, std::chrono::seconds(60)).has_value());
    EXPECT_TRUE(limiter_->tryAcquire(user43, 1, std::chrono::seconds(60)).has_value());
}

TEST_F(RateLimiterRedisIntegration, BucketRefillsOverWindow) {
    const auto key = config_.key_prefix + "ip:127.0.0.1";

    EXPECT_TRUE(limiter_->tryAcquire(key, 1, std::chrono::seconds(1)).has_value());
    EXPECT_FALSE(limiter_->tryAcquire(key, 1, std::chrono::seconds(1)).has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_TRUE(limiter_->tryAcquire(key, 1, std::chrono::seconds(1)).has_value());
}
