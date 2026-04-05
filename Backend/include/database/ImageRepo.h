#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "models/image_generation.h"

struct ImagePageResult {
    std::vector<models::ImageGeneration> content;
    int64_t total_elements{0};
};

class ImageRepo {
  public:
    int64_t insert(const models::ImageGeneration& generation);

    ImagePageResult findByUserId(int64_t userId, int page, int size);
    ImagePageResult findByUserIdAndStatus(int64_t userId, models::TaskStatus status, int page,
                                          int size);

    [[nodiscard]] std::optional<models::ImageGeneration> findByIdAndUserId(int64_t id,
                                                                           int64_t userId);
    bool deleteByIdAndUserId(int64_t id, int64_t userId);

    [[nodiscard]] std::optional<models::ImageGeneration>
    findByRequestIdAndUserId(const std::string& requestId, int64_t userId);
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
                                           models::ImageGeneration* updated = nullptr);
    [[nodiscard]] bool retryByIdAndUserId(int64_t id, int64_t userId,
                                          models::ImageGeneration* updated = nullptr);

    int expireLeases();

    [[nodiscard]] bool updateStatusAndError(int64_t id, int64_t userId, models::TaskStatus status,
                                            const std::string& errorMessage = std::string{});

  private:
    static void ensureTable();
};
