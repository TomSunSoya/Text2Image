#include "services/refresh_token_store.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>

#include "services/redis_client.h"

namespace {

std::string refreshTokenKey(const std::string& jti) {
    return std::format("zimage:refresh:{}", jti);
}

std::string userRefreshSetKey(int64_t userId) {
    return std::format("zimage:refresh:user:{}", userId);
}

ServiceError refreshStoreUnavailable(std::string message) {
    return ServiceError{drogon::k503ServiceUnavailable, "refresh_store_unavailable",
                        std::move(message)};
}

std::shared_ptr<IRefreshTokenStore>& testingStoreOverride() {
    static std::shared_ptr<IRefreshTokenStore> store;
    return store;
}

std::shared_ptr<IRefreshTokenStore>& configuredDefaultStore() {
    static std::shared_ptr<IRefreshTokenStore> store = std::make_shared<RedisRefreshTokenStore>();
    return store;
}

} // namespace

std::expected<void, ServiceError> RedisRefreshTokenStore::store(std::string jti, int64_t userId,
                                                                std::chrono::seconds ttl) {
    try {
        auto& redis = redis::RedisClient::instance();
        if (!redis.isAvailable()) {
            return std::unexpected(refreshStoreUnavailable("refresh token store is unavailable"));
        }
        redis.setex(refreshTokenKey(jti), std::to_string(userId),
                    (std::max)(ttl, std::chrono::seconds{1}));
        redis.sadd(userRefreshSetKey(userId), jti);
        redis.expire(userRefreshSetKey(userId), (std::max)(ttl, std::chrono::seconds{1}));
        return {};
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to store refresh token jti={}: {}", jti, ex.what());
        return std::unexpected(refreshStoreUnavailable("refresh token store failed"));
    }
}

std::expected<std::optional<int64_t>, ServiceError>
RedisRefreshTokenStore::consume(std::string jti) {
    try {
        auto& redis = redis::RedisClient::instance();
        if (!redis.isAvailable()) {
            return std::unexpected(refreshStoreUnavailable("refresh token store is unavailable"));
        }
        const auto value = redis.getDel(refreshTokenKey(jti));
        if (!value) {
            return std::optional<int64_t>{};
        }
        const auto userId = std::stoll(*value);
        redis.srem(userRefreshSetKey(userId), jti);
        return userId;
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to consume refresh token jti={}: {}", jti, ex.what());
        return std::unexpected(refreshStoreUnavailable("refresh token store failed"));
    }
}

std::expected<void, ServiceError> RedisRefreshTokenStore::revoke(std::string jti) {
    try {
        auto& redis = redis::RedisClient::instance();
        if (!redis.isAvailable()) {
            return std::unexpected(refreshStoreUnavailable("refresh token store is unavailable"));
        }
        if (const auto userId = redis.get(refreshTokenKey(jti))) {
            redis.srem(userRefreshSetKey(std::stoll(*userId)), jti);
        }
        redis.del(refreshTokenKey(jti));
        return {};
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to revoke refresh token jti={}: {}", jti, ex.what());
        return std::unexpected(refreshStoreUnavailable("refresh token store failed"));
    }
}

std::expected<void, ServiceError> RedisRefreshTokenStore::revokeUser(int64_t userId) {
    try {
        auto& redis = redis::RedisClient::instance();
        if (!redis.isAvailable()) {
            return std::unexpected(refreshStoreUnavailable("refresh token store is unavailable"));
        }
        const auto userKey = userRefreshSetKey(userId);
        for (const auto& jti : redis.smembers(userKey)) {
            redis.del(refreshTokenKey(jti));
        }
        redis.del(userKey);
        return {};
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to revoke refresh tokens for user_id={}: {}", userId, ex.what());
        return std::unexpected(refreshStoreUnavailable("refresh token store failed"));
    }
}

std::expected<void, ServiceError> InMemoryRefreshTokenStore::store(std::string jti, int64_t userId,
                                                                   std::chrono::seconds ttl) {
    std::lock_guard lock(mutex_);
    tokens_[std::move(jti)] =
        Entry{userId, std::chrono::system_clock::now() + (std::max)(ttl, std::chrono::seconds{1})};
    return {};
}

std::expected<std::optional<int64_t>, ServiceError>
InMemoryRefreshTokenStore::consume(std::string jti) {
    std::lock_guard lock(mutex_);
    eraseExpiredLocked(std::chrono::system_clock::now());

    const auto it = tokens_.find(jti);
    if (it == tokens_.end()) {
        return std::optional<int64_t>{};
    }

    const auto userId = it->second.user_id;
    tokens_.erase(it);
    return userId;
}

std::expected<void, ServiceError> InMemoryRefreshTokenStore::revoke(std::string jti) {
    std::lock_guard lock(mutex_);
    tokens_.erase(jti);
    return {};
}

std::expected<void, ServiceError> InMemoryRefreshTokenStore::revokeUser(int64_t userId) {
    std::lock_guard lock(mutex_);
    std::erase_if(tokens_, [userId](const auto& item) { return item.second.user_id == userId; });
    return {};
}

void InMemoryRefreshTokenStore::clear() {
    std::lock_guard lock(mutex_);
    tokens_.clear();
}

void InMemoryRefreshTokenStore::eraseExpiredLocked(std::chrono::system_clock::time_point now) {
    std::erase_if(tokens_, [now](const auto& item) { return item.second.expires_at <= now; });
}

std::shared_ptr<IRefreshTokenStore> defaultRefreshTokenStore() {
    if (auto& override = testingStoreOverride()) {
        return override;
    }
    return configuredDefaultStore();
}

void setDefaultRefreshTokenStoreForTesting(std::shared_ptr<IRefreshTokenStore> store) {
    testingStoreOverride() = std::move(store);
}
