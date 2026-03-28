#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "Backend.h"
#include "redis_client.h"

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

std::filesystem::path writeTempConfig(const nlohmann::json& config) {
    const auto fileName =
        "backend-redis-config-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
    const auto path = std::filesystem::temp_directory_path() / fileName;

    std::ofstream out(path);
    out << config.dump(2);
    out.close();

    return path;
}

} // namespace

TEST(RedisConfig, ParseUsesDefaultsWhenMissing) {
    const auto cfg = redis::parseRedisConfig(nlohmann::json::object());

    EXPECT_EQ(cfg.host, "127.0.0.1");
    EXPECT_EQ(cfg.port, 6379);
    EXPECT_TRUE(cfg.password.empty());
    EXPECT_EQ(cfg.db, 0);
    EXPECT_EQ(cfg.pool_size, 4);
    EXPECT_EQ(cfg.connect_timeout_ms, 1000);
    EXPECT_EQ(cfg.socket_timeout_ms, 5000);
    EXPECT_EQ(cfg.task_queue_key, "zimage:task_queue");
    EXPECT_EQ(cfg.lease_key_prefix, "zimage:lease:");
    EXPECT_TRUE(cfg.enabled);
}

TEST(RedisConfig, ParseReadsAllFields) {
    const nlohmann::json json = {
        {"host", "cache.internal"},
        {"port", 6380},
        {"password", "secret"},
        {"db", 2},
        {"pool_size", 8},
        {"connect_timeout_ms", 2500},
        {"socket_timeout_ms", 4000},
        {"task_queue_key", "custom:queue"},
        {"lease_key_prefix", "custom:lease:"},
        {"enabled", false},
    };

    const auto cfg = redis::parseRedisConfig(json);

    EXPECT_EQ(cfg.host, "cache.internal");
    EXPECT_EQ(cfg.port, 6380);
    EXPECT_EQ(cfg.password, "secret");
    EXPECT_EQ(cfg.db, 2);
    EXPECT_EQ(cfg.pool_size, 8);
    EXPECT_EQ(cfg.connect_timeout_ms, 2500);
    EXPECT_EQ(cfg.socket_timeout_ms, 4000);
    EXPECT_EQ(cfg.task_queue_key, "custom:queue");
    EXPECT_EQ(cfg.lease_key_prefix, "custom:lease:");
    EXPECT_FALSE(cfg.enabled);
}

TEST(BackendConfig, LoadConfigAppliesRedisEnvOverrides) {
    const ScopedEnvVar hostOverride("REDIS_HOST", std::string("cache.internal"));
    const ScopedEnvVar portOverride("REDIS_PORT", std::string("6381"));
    const ScopedEnvVar queueOverride("REDIS_TASK_QUEUE_KEY", std::string("override:queue"));
    const ScopedEnvVar leaseOverride("REDIS_LEASE_KEY_PREFIX", std::string("override:lease:"));
    const ScopedEnvVar timeoutOverride("REDIS_SOCKET_TIMEOUT_MS", std::string("5000"));
    const ScopedEnvVar enabledOverride("REDIS_ENABLED", std::string("false"));

    const auto path = writeTempConfig({{"redis", {{"host", "127.0.0.1"}, {"enabled", true}}}});

    const auto config = backend::loadConfig(path.string());
    const auto& redis = config.at("redis");

    EXPECT_EQ(redis.at("host").get<std::string>(), "cache.internal");
    EXPECT_EQ(redis.at("port").get<int>(), 6381);
    EXPECT_EQ(redis.at("task_queue_key").get<std::string>(), "override:queue");
    EXPECT_EQ(redis.at("lease_key_prefix").get<std::string>(), "override:lease:");
    EXPECT_EQ(redis.at("socket_timeout_ms").get<int>(), 5000);
    EXPECT_FALSE(redis.at("enabled").get<bool>());

    std::filesystem::remove(path);
}
