#include "services/cache_client.h"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <sw/redis++/redis++.h>

#include <format>
#include <string>
#include <vector>

CacheConfig parseCacheConfig(const nlohmann::json& j) {
    CacheConfig c;
    c.enabled = j.value("enabled", c.enabled);
    c.host = j.value("host", c.host);
    c.port = j.value("port", c.port);
    c.password = j.value("password", c.password);
    c.db = j.value("db", c.db);
    c.pool_size = j.value("pool_size", c.pool_size);
    c.connect_timeout_ms = j.value("connect_timeout_ms", c.connect_timeout_ms);
    c.socket_timeout_ms = j.value("socket_timeout_ms", c.socket_timeout_ms);
    c.key_prefix = j.value("key_prefix", c.key_prefix);
    c.version_key_ttl =
        std::chrono::seconds(j.value("version_key_ttl_seconds", c.version_key_ttl.count()));

    c.port = std::clamp(c.port, 1, 65535);
    c.db = (std::max)(0, c.db);
    c.pool_size = (std::max)(1, c.pool_size);
    c.connect_timeout_ms = (std::max)(1, c.connect_timeout_ms);
    c.socket_timeout_ms = (std::max)(1, c.socket_timeout_ms);
    c.version_key_ttl = (std::max)(std::chrono::seconds{1}, c.version_key_ttl);

    return c;
}

namespace cache {
struct RedisCacheClient::Impl {
    sw::redis::Redis redis;

    explicit Impl(const CacheConfig& cfg) : redis(buildOpts(cfg), buildPoolOpts(cfg)) {}

  private:
    static sw::redis::ConnectionOptions buildOpts(const CacheConfig& cfg) {
        sw::redis::ConnectionOptions opts;
        opts.host = cfg.host;
        opts.port = cfg.port;
        if (!cfg.password.empty())
            opts.password = cfg.password;
        opts.db = cfg.db;
        opts.connect_timeout = std::chrono::milliseconds(cfg.connect_timeout_ms);
        opts.socket_timeout = std::chrono::milliseconds(cfg.socket_timeout_ms);
        return opts;
    }

    static sw::redis::ConnectionPoolOptions buildPoolOpts(const CacheConfig& cfg) {
        sw::redis::ConnectionPoolOptions opts;
        opts.size = static_cast<std::size_t>((std::max)(1, cfg.pool_size));
        return opts;
    }
};

RedisCacheClient::RedisCacheClient(const CacheConfig& config) : config_(config) {
    if (!config_.enabled) {
        spdlog::info("Cache is disabled by configuration");
        return;
    }

    try {
        impl_ = std::make_unique<Impl>(config_);
        impl_->redis.ping();
        available_.store(true, std::memory_order_release);
        spdlog::info("Connected to Redis cache at {}:{}(db = {})", config_.host, config_.port,
                     config_.db);
    } catch (const std::exception& ex) {
        spdlog::warn("Cache init failed, running without cache: {}", ex.what());
        impl_.reset();
        // keep available_ as false to indicate cache is not usable
    }
}

RedisCacheClient::~RedisCacheClient() = default;

bool RedisCacheClient::isAvailable() const noexcept {
    return available_.load(std::memory_order_acquire);
}

std::optional<std::string> RedisCacheClient::get(std::string_view key) const {
    if (!isAvailable()) {
        return std::nullopt;
    }
    try {
        const auto fullKey = cacheKey(key);
        const auto value = impl_->redis.get(fullKey);
        if (!value) {
            return std::nullopt;
        }
        return *value;
    } catch (const std::exception& ex) {
        spdlog::warn("Cache get operation failed for key '{}': {}", key, ex.what());
        available_.store(false, std::memory_order_release);
        return std::nullopt;
    }
}

bool RedisCacheClient::setex(std::string_view key, std::string_view value,
                             std::chrono::seconds ttl) {
    if (!isAvailable()) {
        return false;
    }
    try {
        const auto fullKey = cacheKey(key);
        impl_->redis.setex(fullKey, ttl, sw::redis::StringView(value.data(), value.size()));
        return true;
    } catch (const std::exception& ex) {
        spdlog::warn("Cache setex operation failed for key '{}': {}", key, ex.what());
        available_.store(false, std::memory_order_release);
        return false;
    }
}

bool RedisCacheClient::del(std::string_view key) {
    if (!isAvailable()) {
        return false;
    }
    try {
        const auto fullKey = cacheKey(key);
        return impl_->redis.del(fullKey) > 0;
    } catch (const std::exception& ex) {
        spdlog::warn("Cache del operation failed for key '{}': {}", key, ex.what());
        available_.store(false, std::memory_order_release);
        return false;
    }
}

std::optional<int64_t> RedisCacheClient::bumpVersion(std::string_view ns,
                                                     std::string_view identifier) {
    if (!isAvailable()) {
        return std::nullopt;
    }

    static const std::string script = R"(
        local v = redis.call('INCR', KEYS[1])
        redis.call('EXPIRE', KEYS[1], ARGV[1])
        return v
    )";
    try {
        const std::vector<std::string> keys{versionKey(ns, identifier)};
        const std::vector<std::string> args{std::to_string(config_.version_key_ttl.count())};
        auto result = impl_->redis.eval<long long>(script, keys.begin(), keys.end(), args.begin(),
                                                   args.end());
        return static_cast<int64_t>(result);
    } catch (const std::exception& ex) {
        spdlog::warn("Cache bumpVersion operation failed for ns '{}', identifier '{}': {}", ns,
                     identifier, ex.what());
        available_.store(false, std::memory_order_release);
        return std::nullopt;
    }
}

int64_t RedisCacheClient::getVersion(std::string_view ns, std::string_view identifier) const {
    if (!isAvailable()) {
        return 0; // treat as version 0 if cache is unavailable
    }
    try {
        auto fullKey = versionKey(ns, identifier);
        auto val = impl_->redis.get(fullKey);
        if (val) {
            return std::stoll(*val);
        } else {
            return 0; // not set means version 0
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Cache getVersion operation failed for ns '{}', identifier '{}': {}", ns,
                     identifier, ex.what());
        available_.store(false, std::memory_order_release);
        return 0; // treat as version 0 on error
    }
}

std::string RedisCacheClient::cacheKey(std::string_view key) const {
    return std::format("{}{}", config_.key_prefix, key);
}

std::string RedisCacheClient::versionKey(std::string_view ns, std::string_view identifier) const {
    return std::format("{}ver:{}:{}", config_.key_prefix, ns, identifier);
}

} // namespace cache
