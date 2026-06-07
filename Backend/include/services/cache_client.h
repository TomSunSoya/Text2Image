#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "services/i_cache_client.h"

struct CacheConfig {
    bool enabled{true};
    std::string host{"127.0.0.1"};
    int port{6379};
    std::string password;
    int db{1}; // different from RedisClient to avoid conflicts
    int pool_size{4};
    int connect_timeout_ms{1000};
    int socket_timeout_ms{500}; // shorter timeout for cache operations, quick failure is preferred
    std::string key_prefix{"zimage:"}; // prefix for all cache keys to avoid conflicts
    std::chrono::seconds version_key_ttl{
        std::chrono::hours{24}}; // TTL for version keys used in cache invalidation
};

CacheConfig parseCacheConfig(const nlohmann::json& j);

namespace cache {
class RedisCacheClient : public ICacheClient {
  public:
    explicit RedisCacheClient(const CacheConfig& config);
    ~RedisCacheClient() override;

    [[nodiscard]] bool isAvailable() const noexcept override;

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const override;

    bool setex(std::string_view key, std::string_view value, std::chrono::seconds ttl) override;
    bool del(std::string_view key) override;

    std::optional<int64_t> bumpVersion(std::string_view ns, std::string_view identifier) override;

    int64_t getVersion(std::string_view ns, std::string_view identifier) const override;

  private:
    [[nodiscard]] std::string cacheKey(std::string_view key) const;
    [[nodiscard]] std::string versionKey(std::string_view ns, std::string_view identifier) const;

  private:
    CacheConfig config_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::atomic_bool available_{false};
};

} // namespace cache
