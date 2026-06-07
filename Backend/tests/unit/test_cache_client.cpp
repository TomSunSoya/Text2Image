#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include "Backend.h"
#include "services/cache_client.h"
#include "services/null_cache_client.h"

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
            "backend-cache-config-test-" +
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

CacheConfig unreachableRedisConfig() {
    CacheConfig cfg;
    cfg.enabled = true;
    cfg.host = "127.0.0.1";
    cfg.port = 1;
    cfg.connect_timeout_ms = 10;
    cfg.socket_timeout_ms = 10;
    cfg.pool_size = 1;
    cfg.key_prefix = "zimage:test:unit:";
    return cfg;
}

} // namespace

TEST(NullCacheClient, ContractReturnsCacheMissAndFalseWrites) {
    cache::NullCacheClient client;

    EXPECT_FALSE(client.isAvailable());
    EXPECT_EQ(client.get("missing"), std::nullopt);
    EXPECT_FALSE(client.setex("key", "value", std::chrono::seconds(30)));
    EXPECT_FALSE(client.del("key"));
    EXPECT_EQ(client.bumpVersion("images", "42"), std::nullopt);
    EXPECT_EQ(client.getVersion("images", "42"), 0);
}

TEST(CacheConfig, ParseUsesDefaultsWhenMissing) {
    const auto cfg = parseCacheConfig(nlohmann::json::object());

    EXPECT_TRUE(cfg.enabled);
    EXPECT_EQ(cfg.host, "127.0.0.1");
    EXPECT_EQ(cfg.port, 6379);
    EXPECT_TRUE(cfg.password.empty());
    EXPECT_EQ(cfg.db, 1);
    EXPECT_EQ(cfg.pool_size, 4);
    EXPECT_EQ(cfg.connect_timeout_ms, 1000);
    EXPECT_EQ(cfg.socket_timeout_ms, 500);
    EXPECT_EQ(cfg.key_prefix, "zimage:");
    EXPECT_EQ(cfg.version_key_ttl.count(), 86400);
}

TEST(CacheConfig, ParseReadsAllFields) {
    const nlohmann::json json = {
        {"enabled", false},
        {"host", "cache.internal"},
        {"port", 6382},
        {"password", "secret"},
        {"db", 3},
        {"pool_size", 9},
        {"connect_timeout_ms", 1500},
        {"socket_timeout_ms", 250},
        {"key_prefix", "custom:"},
        {"version_key_ttl_seconds", 120},
    };

    const auto cfg = parseCacheConfig(json);

    EXPECT_FALSE(cfg.enabled);
    EXPECT_EQ(cfg.host, "cache.internal");
    EXPECT_EQ(cfg.port, 6382);
    EXPECT_EQ(cfg.password, "secret");
    EXPECT_EQ(cfg.db, 3);
    EXPECT_EQ(cfg.pool_size, 9);
    EXPECT_EQ(cfg.connect_timeout_ms, 1500);
    EXPECT_EQ(cfg.socket_timeout_ms, 250);
    EXPECT_EQ(cfg.key_prefix, "custom:");
    EXPECT_EQ(cfg.version_key_ttl.count(), 120);
}

TEST(CacheConfig, ParseClampsUnsafeValues) {
    const nlohmann::json json = {
        {"port", 0},
        {"db", -1},
        {"pool_size", -1},
        {"connect_timeout_ms", 0},
        {"socket_timeout_ms", -20},
        {"version_key_ttl_seconds", 0},
    };

    const auto cfg = parseCacheConfig(json);

    EXPECT_EQ(cfg.port, 1);
    EXPECT_EQ(cfg.db, 0);
    EXPECT_EQ(cfg.pool_size, 1);
    EXPECT_EQ(cfg.connect_timeout_ms, 1);
    EXPECT_EQ(cfg.socket_timeout_ms, 1);
    EXPECT_EQ(cfg.version_key_ttl.count(), 1);
}

TEST(CacheConfig, ParseClampsPortUpperBound) {
    const auto cfg = parseCacheConfig({{"port", 70000}});

    EXPECT_EQ(cfg.port, 65535);
}

TEST(BackendConfig, LoadConfigAppliesCacheEnvOverrides) {
    const ScopedEnvVar hostOverride("CACHE_HOST", std::string("cache.internal"));
    const ScopedEnvVar portOverride("CACHE_PORT", std::string("6382"));
    const ScopedEnvVar passwordOverride("CACHE_PASSWORD", std::string("cache-secret"));
    const ScopedEnvVar dbOverride("CACHE_DB", std::string("3"));
    const ScopedEnvVar prefixOverride("CACHE_KEY_PREFIX", std::string("override:"));
    const ScopedEnvVar ttlOverride("CACHE_VERSION_KEY_TTL_SECONDS", std::string("120"));
    const ScopedEnvVar enabledOverride("CACHE_ENABLED", std::string("false"));

    const TempConfigFile configFile(
        {{"cache", {{"host", "127.0.0.1"}, {"enabled", true}}}});

    const auto config = backend::loadConfig(configFile.path().string());
    const auto& cache = config.at("cache");

    EXPECT_EQ(cache.at("host").get<std::string>(), "cache.internal");
    EXPECT_EQ(cache.at("port").get<int>(), 6382);
    EXPECT_EQ(cache.at("password").get<std::string>(), "cache-secret");
    EXPECT_EQ(cache.at("db").get<int>(), 3);
    EXPECT_EQ(cache.at("key_prefix").get<std::string>(), "override:");
    EXPECT_EQ(cache.at("version_key_ttl_seconds").get<int>(), 120);
    EXPECT_FALSE(cache.at("enabled").get<bool>());
}

TEST(RedisCacheClient, UnreachableRedisFallsBackToUnavailable) {
    cache::RedisCacheClient client(unreachableRedisConfig());

    EXPECT_FALSE(client.isAvailable());
    EXPECT_EQ(client.get("key"), std::nullopt);
    EXPECT_FALSE(client.setex("key", "value", std::chrono::seconds(30)));
    EXPECT_FALSE(client.del("key"));
    EXPECT_EQ(client.bumpVersion("images", "42"), std::nullopt);
    EXPECT_EQ(client.getVersion("images", "42"), 0);
}

TEST(RedisCacheClient, DisabledConfigDoesNotConnect) {
    auto cfg = unreachableRedisConfig();
    cfg.enabled = false;

    cache::RedisCacheClient client(cfg);

    EXPECT_FALSE(client.isAvailable());
    EXPECT_EQ(client.get("key"), std::nullopt);
    EXPECT_FALSE(client.setex("key", "value", std::chrono::seconds(30)));
    EXPECT_FALSE(client.del("key"));
    EXPECT_EQ(client.bumpVersion("images", "42"), std::nullopt);
    EXPECT_EQ(client.getVersion("images", "42"), 0);
}
