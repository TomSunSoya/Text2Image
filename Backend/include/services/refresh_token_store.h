#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "services/service_error.h"

class IRefreshTokenStore {
  public:
    virtual ~IRefreshTokenStore() = default;

    [[nodiscard]] virtual std::expected<void, ServiceError> store(std::string jti, int64_t userId,
                                                                  std::chrono::seconds ttl) = 0;
    [[nodiscard]] virtual std::expected<std::optional<int64_t>, ServiceError>
    consume(std::string jti) = 0;
    [[nodiscard]] virtual std::expected<void, ServiceError> revoke(std::string jti) = 0;
    [[nodiscard]] virtual std::expected<void, ServiceError> revokeUser(int64_t userId) = 0;
};

class RedisRefreshTokenStore final : public IRefreshTokenStore {
  public:
    [[nodiscard]] std::expected<void, ServiceError> store(std::string jti, int64_t userId,
                                                          std::chrono::seconds ttl) override;
    [[nodiscard]] std::expected<std::optional<int64_t>, ServiceError>
    consume(std::string jti) override;
    [[nodiscard]] std::expected<void, ServiceError> revoke(std::string jti) override;
    [[nodiscard]] std::expected<void, ServiceError> revokeUser(int64_t userId) override;
};

class InMemoryRefreshTokenStore final : public IRefreshTokenStore {
  public:
    [[nodiscard]] std::expected<void, ServiceError> store(std::string jti, int64_t userId,
                                                          std::chrono::seconds ttl) override;
    [[nodiscard]] std::expected<std::optional<int64_t>, ServiceError>
    consume(std::string jti) override;
    [[nodiscard]] std::expected<void, ServiceError> revoke(std::string jti) override;
    [[nodiscard]] std::expected<void, ServiceError> revokeUser(int64_t userId) override;
    void clear();

  private:
    struct Entry {
        int64_t user_id{0};
        std::chrono::system_clock::time_point expires_at{};
    };

    void eraseExpiredLocked(std::chrono::system_clock::time_point now);

    std::mutex mutex_;
    std::unordered_map<std::string, Entry> tokens_;
};

std::shared_ptr<IRefreshTokenStore> defaultRefreshTokenStore();
void setDefaultRefreshTokenStoreForTesting(std::shared_ptr<IRefreshTokenStore> store);
