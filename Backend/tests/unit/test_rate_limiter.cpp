#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "Backend.h"
#include "services/rate_limiter.h"

namespace {

std::optional<std::string> readEnvVar(const char* name) {
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
    return value;
}

class ScopedEnvVar {
  public:
    ScopedEnvVar(const char* name, std::optional<std::string> value)
        : name_(name), original_(readEnvVar(name)) {
        set(value);
    }

    ~ScopedEnvVar() {
        set(original_);
    }

  private:
    void set(const std::optional<std::string>& value) {
#ifdef _WIN32
        if (value.has_value()) {
            _putenv_s(name_.c_str(), value->c_str());
        } else {
            _putenv_s(name_.c_str(), "");
        }
#else
        if (value.has_value()) {
            setenv(name_.c_str(), value->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
#endif
    }

    std::string name_;
    std::optional<std::string> original_;
};

class TempConfigFile {
  public:
    explicit TempConfigFile(const nlohmann::json& config) {
        const auto fileName =
            "backend-rate-limit-config-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
        path_ = std::filesystem::temp_directory_path() / fileName;

        std::ofstream out(path_);
        out << config.dump(2);
    }

    ~TempConfigFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

} // namespace

TEST(RateLimitConfig, ParseUsesDefaultsWhenMissing) {
    const auto config =
        rate_limit::parseRateLimitConfig(nlohmann::json::object(), redis::RedisConfig{});

    EXPECT_TRUE(config.enabled);
    EXPECT_TRUE(config.fail_open);
    EXPECT_EQ(config.max_active_tasks_per_user, 3);
    EXPECT_EQ(config.user_create_capacity, 10);
    EXPECT_EQ(config.user_create_window, std::chrono::seconds(60));
    EXPECT_EQ(config.auth_ip_capacity, 5);
    EXPECT_EQ(config.auth_ip_window, std::chrono::seconds(60));
    EXPECT_EQ(config.key_prefix, "zimage:rate:");
}

TEST(RateLimitConfig, ParseReadsAndClampsValues) {
    const nlohmann::json json = {
        {"enabled", false},
        {"fail_open", false},
        {"max_active_tasks_per_user", -1},
        {"user_create_capacity", -10},
        {"user_create_window_seconds", 0},
        {"auth_ip_capacity", 7},
        {"auth_ip_window_seconds", 30},
        {"key_prefix", "custom:rate"},
    };

    auto redisConfig = redis::RedisConfig{};
    redisConfig.socket_timeout_ms = 5000;
    const auto config = rate_limit::parseRateLimitConfig(json, redisConfig);

    EXPECT_FALSE(config.enabled);
    EXPECT_FALSE(config.fail_open);
    EXPECT_EQ(config.max_active_tasks_per_user, 0);
    EXPECT_EQ(config.user_create_capacity, 0);
    EXPECT_EQ(config.user_create_window, std::chrono::seconds(1));
    EXPECT_EQ(config.auth_ip_capacity, 7);
    EXPECT_EQ(config.auth_ip_window, std::chrono::seconds(30));
    EXPECT_EQ(config.key_prefix, "custom:rate:");
    EXPECT_EQ(config.redis.socket_timeout_ms, 500);
}

TEST(RateLimitConfig, LoadConfigAppliesEnvOverrides) {
    const ScopedEnvVar enabledOverride("RATE_LIMIT_ENABLED", std::string("false"));
    const ScopedEnvVar failOpenOverride("RATE_LIMIT_FAIL_OPEN", std::string("false"));
    const ScopedEnvVar activeOverride("RATE_LIMIT_MAX_ACTIVE_TASKS_PER_USER", std::string("9"));
    const ScopedEnvVar userCapacityOverride("RATE_LIMIT_USER_CREATE_CAPACITY", std::string("11"));
    const ScopedEnvVar userWindowOverride("RATE_LIMIT_USER_CREATE_WINDOW_SECONDS",
                                          std::string("45"));
    const ScopedEnvVar authCapacityOverride("RATE_LIMIT_AUTH_IP_CAPACITY", std::string("6"));
    const ScopedEnvVar authWindowOverride("RATE_LIMIT_AUTH_IP_WINDOW_SECONDS", std::string("20"));
    const ScopedEnvVar prefixOverride("RATE_LIMIT_KEY_PREFIX", std::string("test:rate:"));

    const TempConfigFile configFile(
        {{"redis", {{"enabled", true}}}, {"rate_limit", {{"enabled", true}, {"fail_open", true}}}});

    const auto config = backend::loadConfig(configFile.path().string());
    const auto rateLimit = rate_limit::loadRateLimitConfig(config);

    EXPECT_FALSE(rateLimit.enabled);
    EXPECT_FALSE(rateLimit.fail_open);
    EXPECT_EQ(rateLimit.max_active_tasks_per_user, 9);
    EXPECT_EQ(rateLimit.user_create_capacity, 11);
    EXPECT_EQ(rateLimit.user_create_window, std::chrono::seconds(45));
    EXPECT_EQ(rateLimit.auth_ip_capacity, 6);
    EXPECT_EQ(rateLimit.auth_ip_window, std::chrono::seconds(20));
    EXPECT_EQ(rateLimit.key_prefix, "test:rate:");
}

TEST(RedisTokenBucketLimiter, UnavailableRedisFailsOpen) {
    auto config = rate_limit::RateLimitConfig{};
    config.fail_open = true;
    config.redis.host = "127.0.0.1";
    config.redis.port = 1;
    config.redis.connect_timeout_ms = 1;
    config.redis.socket_timeout_ms = 1;

    rate_limit::RedisTokenBucketLimiter limiter(config);

    const auto acquired = limiter.tryAcquire("zimage:rate:test", 1, std::chrono::seconds(60));

    EXPECT_TRUE(acquired.has_value());
    EXPECT_FALSE(limiter.isAvailable());
}
