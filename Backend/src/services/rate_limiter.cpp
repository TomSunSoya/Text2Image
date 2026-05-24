#include "services/rate_limiter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sw/redis++/redis++.h>

#include "Backend.h"

namespace {

constexpr std::string_view kTokenBucketScript = R"(
local key = KEYS[1]
local capacity = tonumber(ARGV[1])
local window_ms = tonumber(ARGV[2])
local now_ms = tonumber(ARGV[3])
local ttl_seconds = tonumber(ARGV[4])

local data = redis.call('HMGET', key, 'tokens', 'refreshed_at')
local tokens = tonumber(data[1])
local refreshed_at = tonumber(data[2])

if tokens == nil or refreshed_at == nil then
    tokens = capacity
    refreshed_at = now_ms
end

local elapsed = math.max(0, now_ms - refreshed_at)
local refill = elapsed * capacity / window_ms
tokens = math.min(capacity, tokens + refill)

local allowed = 0
if tokens >= 1 then
    tokens = tokens - 1
    allowed = 1
end

redis.call('HMSET', key, 'tokens', tokens, 'refreshed_at', now_ms)
redis.call('EXPIRE', key, ttl_seconds)

return allowed
)";

std::chrono::milliseconds nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
}

sw::redis::ConnectionOptions buildRedisOptions(const redis::RedisConfig& cfg) {
    sw::redis::ConnectionOptions opts;
    opts.host = cfg.host;
    opts.port = cfg.port;
    if (!cfg.password.empty()) {
        opts.password = cfg.password;
    }
    opts.db = cfg.db;
    opts.connect_timeout = std::chrono::milliseconds(cfg.connect_timeout_ms);
    opts.socket_timeout = std::chrono::milliseconds(cfg.socket_timeout_ms);
    return opts;
}

sw::redis::ConnectionPoolOptions buildRedisPoolOptions(const redis::RedisConfig& cfg) {
    sw::redis::ConnectionPoolOptions opts;
    opts.size = static_cast<std::size_t>((std::max)(1, cfg.pool_size));
    return opts;
}

bool looksLikeNoScript(const std::exception& ex) {
    const std::string message = ex.what();
    return message.find("NOSCRIPT") != std::string::npos ||
           message.find("No matching script") != std::string::npos;
}

std::shared_ptr<IRateLimiter> makeLimiter(const rate_limit::RateLimitConfig& config) {
    if (!config.enabled || !config.redis.enabled) {
        return std::make_shared<rate_limit::NullRateLimiter>();
    }
    return std::make_shared<rate_limit::RedisTokenBucketLimiter>(config);
}

rate_limit::RateLimitConfig& configuredDefaultConfig() {
    static rate_limit::RateLimitConfig config{};
    return config;
}

std::shared_ptr<IRateLimiter>& configuredDefaultLimiter() {
    static std::shared_ptr<IRateLimiter> limiter = makeLimiter(configuredDefaultConfig());
    return limiter;
}

std::shared_ptr<IRateLimiter>& testingLimiterOverride() {
    static std::shared_ptr<IRateLimiter> limiter;
    return limiter;
}

} // namespace

namespace rate_limit {

struct RedisTokenBucketLimiter::Impl {
    sw::redis::Redis redis;

    explicit Impl(const redis::RedisConfig& cfg)
        : redis(buildRedisOptions(cfg), buildRedisPoolOptions(cfg)) {}
};

RateLimitConfig parseRateLimitConfig(const nlohmann::json& rateLimitJson,
                                     const redis::RedisConfig& redisConfig) {
    RateLimitConfig config;
    config.redis = redisConfig;

    if (!rateLimitJson.is_object()) {
        config.redis.socket_timeout_ms = (std::min)(config.redis.socket_timeout_ms, 500);
        return config;
    }

    config.enabled = rateLimitJson.value("enabled", config.enabled);
    config.fail_open = rateLimitJson.value("fail_open", config.fail_open);
    config.trust_proxy = rateLimitJson.value("trust_proxy", config.trust_proxy);
    config.max_active_tasks_per_user =
        rateLimitJson.value("max_active_tasks_per_user", config.max_active_tasks_per_user);
    config.user_create_capacity =
        rateLimitJson.value("user_create_capacity", config.user_create_capacity);
    config.user_create_window = std::chrono::seconds(
        rateLimitJson.value("user_create_window_seconds", config.user_create_window.count()));
    config.auth_ip_capacity = rateLimitJson.value("auth_ip_capacity", config.auth_ip_capacity);
    config.auth_ip_window = std::chrono::seconds(
        rateLimitJson.value("auth_ip_window_seconds", config.auth_ip_window.count()));
    config.key_prefix = rateLimitJson.value("key_prefix", config.key_prefix);

    config.max_active_tasks_per_user = (std::max)(0, config.max_active_tasks_per_user);
    config.user_create_capacity = (std::max)(0, config.user_create_capacity);
    config.auth_ip_capacity = (std::max)(0, config.auth_ip_capacity);
    config.user_create_window = (std::max)(std::chrono::seconds{1}, config.user_create_window);
    config.auth_ip_window = (std::max)(std::chrono::seconds{1}, config.auth_ip_window);
    config.redis.socket_timeout_ms = (std::min)(config.redis.socket_timeout_ms, 500);

    if (!config.key_prefix.ends_with(":")) {
        config.key_prefix += ":";
    }

    return config;
}

RateLimitConfig loadRateLimitConfig(const nlohmann::json& backendConfig) {
    const auto redisConfig =
        backendConfig.contains("redis") && backendConfig.at("redis").is_object()
            ? redis::parseRedisConfig(backendConfig.at("redis"))
            : redis::RedisConfig{};
    const auto rateJson = backendConfig.contains("rate_limit") ? backendConfig.at("rate_limit")
                                                               : nlohmann::json::object();
    return parseRateLimitConfig(rateJson, redisConfig);
}

RateLimitConfig loadRateLimitConfig() {
    return loadRateLimitConfig(backend::cachedConfig());
}

std::string userKey(int64_t userId, const RateLimitConfig& config) {
    return std::format("{}user:{}", config.key_prefix, userId);
}

std::string ipKey(std::string_view ip, const RateLimitConfig& config) {
    const auto safeIp = ip.empty() ? std::string_view{"unknown"} : ip;
    return std::format("{}ip:{}", config.key_prefix, safeIp);
}

RedisTokenBucketLimiter::RedisTokenBucketLimiter(RateLimitConfig config)
    : config_(std::move(config)) {
    if (!config_.enabled || !config_.redis.enabled) {
        spdlog::info("Rate limiter disabled by configuration");
        return;
    }

    try {
        impl_ = std::make_unique<Impl>(config_.redis);
        impl_->redis.ping();
        loadScript();
        available_.store(true, std::memory_order_release);
        spdlog::info("Connected to Redis rate limiter at {}:{}(db = {})", config_.redis.host,
                     config_.redis.port, config_.redis.db);
    } catch (const std::exception& ex) {
        spdlog::warn("Rate limiter init failed, running fail-open: {}", ex.what());
        impl_.reset();
        available_.store(false, std::memory_order_release);
    }
}

RedisTokenBucketLimiter::~RedisTokenBucketLimiter() = default;

bool RedisTokenBucketLimiter::isAvailable() const noexcept {
    return available_.load(std::memory_order_acquire) && impl_ != nullptr;
}

std::expected<void, ServiceError> RedisTokenBucketLimiter::tryAcquire(std::string_view key,
                                                                      int capacity,
                                                                      std::chrono::seconds window) {
    if (!config_.enabled || capacity <= 0 || window <= std::chrono::seconds{0}) {
        return {};
    }

    if (!isAvailable()) {
        if (config_.fail_open) {
            spdlog::warn("Rate limiter unavailable; allowing request for key '{}'", key);
            return {};
        }
        return std::unexpected(ServiceError{drogon::k503ServiceUnavailable,
                                            "rate_limiter_unavailable",
                                            "rate limiter is unavailable"});
    }

    try {
        const auto allowed = evalTokenBucket(key, capacity, window);
        if (allowed == 1) {
            return {};
        }
        return std::unexpected(
            ServiceError::tooManyRequests("rate_limit_exceeded", "too many requests"));
    } catch (const std::exception& ex) {
        spdlog::warn("Rate limiter operation failed for key '{}': {}", key, ex.what());
        available_.store(false, std::memory_order_release);
        if (config_.fail_open) {
            return {};
        }
        return std::unexpected(ServiceError{drogon::k503ServiceUnavailable,
                                            "rate_limiter_unavailable",
                                            "rate limiter is unavailable"});
    }
}

long long RedisTokenBucketLimiter::evalTokenBucket(std::string_view key, int capacity,
                                                   std::chrono::seconds window) {
    const auto now = nowMs().count();
    const auto windowMs = std::chrono::duration_cast<std::chrono::milliseconds>(window).count();
    const auto ttlSeconds = (std::max)(1LL, window.count() * 2);

    const std::vector<std::string> keys{std::string{key}};
    const std::vector<std::string> args{std::to_string(capacity), std::to_string(windowMs),
                                        std::to_string(now), std::to_string(ttlSeconds)};

    try {
        return impl_->redis.evalsha<long long>(script_sha_, keys.begin(), keys.end(), args.begin(),
                                               args.end());
    } catch (const sw::redis::Error& ex) {
        if (!looksLikeNoScript(ex)) {
            throw;
        }
        loadScript();
        return impl_->redis.evalsha<long long>(script_sha_, keys.begin(), keys.end(), args.begin(),
                                               args.end());
    }
}

void RedisTokenBucketLimiter::loadScript() {
    std::scoped_lock lock(script_mutex_);
    script_sha_ = impl_->redis.script_load(
        sw::redis::StringView(kTokenBucketScript.data(), kTokenBucketScript.size()));
}

void configureDefaultRateLimiter(const nlohmann::json& backendConfig) {
    configuredDefaultConfig() = loadRateLimitConfig(backendConfig);
    configuredDefaultLimiter() = makeLimiter(configuredDefaultConfig());
}

const RateLimitConfig& defaultRateLimitConfig() {
    return configuredDefaultConfig();
}

std::shared_ptr<IRateLimiter> defaultRateLimiter() {
    if (auto& override = testingLimiterOverride()) {
        return override;
    }
    return configuredDefaultLimiter();
}

void setDefaultRateLimiterForTesting(std::shared_ptr<IRateLimiter> limiter) {
    testingLimiterOverride() = std::move(limiter);
}

} // namespace rate_limit
