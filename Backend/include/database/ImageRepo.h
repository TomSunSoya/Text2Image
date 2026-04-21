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
    int64_t insert(const models::ImageGeneration& generation) override;

    ImagePageResult findByUserId(int64_t userId, int page, int size) override;
    ImagePageResult findByUserIdAndStatus(int64_t userId, models::TaskStatus status, int page,
                                          int size) override;

    [[nodiscard]] std::optional<models::ImageGeneration>
    findByIdAndUserId(int64_t id, int64_t userId) override;
    bool deleteByIdAndUserId(int64_t id, int64_t userId) override;

    [[nodiscard]] std::optional<models::ImageGeneration>
    findByRequestIdAndUserId(const std::string& requestId, int64_t userId) override;
    [[nodiscard]] std::optional<models::ImageGeneration> claimNextTask(const std::string& workerId,
                                                                       long leaseSeconds);
    [[nodiscard]] std::optional<models::ImageGeneration>
    claimTaskById(int64_t taskId, const std::string& workerId, long leaseSeconds);
    std::vector<int64_t> findQueuedTaskIds();
    std::vector<int64_t> expireLeasesReturningIds();
    [[nodiscard]] bool renewLease(int64_t id, int64_t userId, const std::string& workerId,
                                  long leaseSeconds);

    [[nodiscard]] bool finishClaimedTask(const models::ImageGeneration& generation);
    [[nodiscard]] bool cancelByIdAndUserId(int64_t id, int64_t userId,
                                           models::ImageGeneration* updated = nullptr) override;
    [[nodiscard]] bool retryByIdAndUserId(int64_t id, int64_t userId,
                                          models::ImageGeneration* updated = nullptr) override;

    int expireLeases();

    [[nodiscard]] bool updateStatusAndError(int64_t id, int64_t userId, models::TaskStatus status,
                                            const std::string& errorMessage = std::string{});

  private:
    static void ensureTable();
};
