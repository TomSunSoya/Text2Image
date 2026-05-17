#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "database/i_image_repo.h"
#include "models/image_generation.h"

class ImageRepo : public IImageRepo {
  public:
    RepoResult<int64_t> insert(const models::ImageGeneration& generation) override;

    RepoResult<ImagePageResult> findByUserId(int64_t userId, int page, int size) override;
    RepoResult<ImagePageResult> findByUserIdAndStatus(int64_t userId, models::TaskStatus status,
                                                      int page, int size) override;

    [[nodiscard]] RepoResult<std::optional<models::ImageGeneration>>
    findByIdAndUserId(int64_t id, int64_t userId) override;
    RepoResult<bool> deleteByIdAndUserId(int64_t id, int64_t userId) override;

    [[nodiscard]] RepoResult<std::optional<models::ImageGeneration>>
    findByRequestIdAndUserId(const std::string& requestId, int64_t userId) override;
    [[nodiscard]] RepoResult<std::optional<models::ImageGeneration>>
    claimNextTask(const std::string& workerId, long leaseSeconds);
    [[nodiscard]] RepoResult<std::optional<models::ImageGeneration>>
    claimTaskById(int64_t taskId, const std::string& workerId, long leaseSeconds);
    RepoResult<std::vector<int64_t>> findQueuedTaskIds();
    RepoResult<std::vector<ExpiredLease>> expireLeasesReturningExpired() override;
    [[nodiscard]] RepoResult<bool> renewLease(int64_t id, int64_t userId,
                                              const std::string& workerId, long leaseSeconds);

    [[nodiscard]] RepoResult<bool> finishClaimedTask(const models::ImageGeneration& generation);
    [[nodiscard]] RepoResult<std::optional<models::ImageGeneration>>
    cancelByIdAndUserId(int64_t id, int64_t userId) override;
    [[nodiscard]] RepoResult<std::optional<models::ImageGeneration>>
    retryByIdAndUserId(int64_t id, int64_t userId) override;

    RepoResult<int> expireLeases();

    [[nodiscard]] RepoResult<bool>
    updateStatusAndError(int64_t id, int64_t userId, models::TaskStatus status,
                         const std::string& errorMessage = std::string{});

  private:
    static void ensureTable();
};
