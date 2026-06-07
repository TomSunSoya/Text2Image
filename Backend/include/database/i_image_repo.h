#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "database/repo_error.h"
#include "models/image_generation.h"

struct ImagePageResult {
    std::vector<models::ImageGeneration> content;
    int64_t total_elements{0};
};

struct ExpiredLease {
    int64_t id{0};
    int64_t user_id{0};
    bool requeue{false};
};

class IImageRepo {
  public:
    virtual ~IImageRepo() = default;

    virtual RepoResult<int64_t> insert(const models::ImageGeneration& generation) = 0;

    virtual RepoResult<ImagePageResult> findByUserId(int64_t userId, int page, int size) = 0;
    virtual RepoResult<ImagePageResult>
    findByUserIdAndStatus(int64_t userId, models::TaskStatus status, int page, int size) = 0;

    [[nodiscard]] virtual RepoResult<std::optional<models::ImageGeneration>>
    findByIdAndUserId(int64_t id, int64_t userId) = 0;

    [[nodiscard]] virtual RepoResult<std::optional<models::ImageGeneration>>
    findByRequestIdAndUserId(const std::string& requestId, int64_t userId) = 0;

    [[nodiscard]] virtual RepoResult<int64_t> countActiveTasksByUserId(int64_t userId) = 0;

    virtual RepoResult<bool> deleteByIdAndUserId(int64_t id, int64_t userId) = 0;

    [[nodiscard]] virtual RepoResult<std::optional<models::ImageGeneration>>
    cancelByIdAndUserId(int64_t id, int64_t userId) = 0;
    [[nodiscard]] virtual RepoResult<std::optional<models::ImageGeneration>>
    retryByIdAndUserId(int64_t id, int64_t userId) = 0;

    virtual RepoResult<std::vector<ExpiredLease>> expireLeasesReturningExpired() = 0;
};
