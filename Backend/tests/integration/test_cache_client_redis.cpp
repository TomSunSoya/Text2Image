#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "services/cache_client.h"

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

CacheConfig redisIntegrationConfig() {
    CacheConfig cfg;
    cfg.enabled = true;
    cfg.host = firstEnv({"TEST_REDIS_HOST", "CACHE_HOST", "REDIS_HOST"}).value_or("127.0.0.1");
    cfg.port = readEnvInt({"TEST_REDIS_PORT", "CACHE_PORT", "REDIS_PORT"}, 6379);
    cfg.password = firstEnv({"TEST_REDIS_PASSWORD", "CACHE_PASSWORD", "REDIS_PASSWORD"})
                       .value_or(std::string{});
    cfg.db = readEnvInt({"TEST_REDIS_DB", "CACHE_DB"}, 15);
    cfg.pool_size = 1;
    cfg.connect_timeout_ms = 200;
    cfg.socket_timeout_ms = 200;
    cfg.version_key_ttl = std::chrono::seconds(60);
    cfg.key_prefix = "zimage:test:cache:" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     ":";
    return cfg;
}

} // namespace

class CacheClientRedisIntegration : public ::testing::Test {
  protected:
    void SetUp() override {
        config_ = redisIntegrationConfig();
        client_ = std::make_unique<cache::RedisCacheClient>(config_);
        if (!client_->isAvailable()) {
            GTEST_SKIP() << "Redis cache integration skipped: " << config_.host << ":"
                         << config_.port << " is unavailable. Set TEST_REDIS_* to run this test.";
        }
    }

    CacheConfig config_;
    std::unique_ptr<cache::RedisCacheClient> client_;
};

TEST_F(CacheClientRedisIntegration, SetGetDeleteRoundTrip) {
    ASSERT_TRUE(client_->setex("kv", "value", std::chrono::seconds(30)));
    EXPECT_EQ(client_->get("kv"), std::optional<std::string>("value"));

    EXPECT_TRUE(client_->del("kv"));
    EXPECT_EQ(client_->get("kv"), std::nullopt);
    EXPECT_FALSE(client_->del("kv"));
}

TEST_F(CacheClientRedisIntegration, SetexExpiresKey) {
    ASSERT_TRUE(client_->setex("short-lived", "value", std::chrono::seconds(1)));
    EXPECT_EQ(client_->get("short-lived"), std::optional<std::string>("value"));

    std::this_thread::sleep_for(std::chrono::seconds(2));
    EXPECT_EQ(client_->get("short-lived"), std::nullopt);
}

TEST_F(CacheClientRedisIntegration, VersionRoundTrip) {
    EXPECT_EQ(client_->getVersion("images", "user-42"), 0);

    const auto v1 = client_->bumpVersion("images", "user-42");
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, 1);
    EXPECT_EQ(client_->getVersion("images", "user-42"), 1);

    const auto v2 = client_->bumpVersion("images", "user-42");
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v2, 2);
    EXPECT_EQ(client_->getVersion("images", "user-42"), 2);
}

TEST_F(CacheClientRedisIntegration, VersionsAreIsolatedAcrossNamespaces) {
    EXPECT_EQ(client_->getVersion("images", "user-43"), 0);
    EXPECT_EQ(client_->getVersion("users", "user-43"), 0);

    ASSERT_EQ(client_->bumpVersion("images", "user-43"), std::optional<int64_t>(1));
    EXPECT_EQ(client_->getVersion("images", "user-43"), 1);
    EXPECT_EQ(client_->getVersion("users", "user-43"), 0);

    ASSERT_EQ(client_->bumpVersion("users", "user-43"), std::optional<int64_t>(1));
    EXPECT_EQ(client_->getVersion("images", "user-43"), 1);
    EXPECT_EQ(client_->getVersion("users", "user-43"), 1);
}
