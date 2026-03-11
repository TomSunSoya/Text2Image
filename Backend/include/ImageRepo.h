#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "image_generation.h"

class ImageRepo {
public:
    int64_t insert(const models::ImageGeneration& generation);

    std::vector<models::ImageGeneration> findByUserId(int64_t userId, int page, int size);
    std::vector<models::ImageGeneration> findByUserIdAndStatus(int64_t userId,
                                                                const std::string& status,
                                                                int page,
                                                                int size);

    int64_t countByUserId(int64_t userId);
    int64_t countByUserIdAndStatus(int64_t userId, const std::string& status);

    std::optional<models::ImageGeneration> findByIdAndUserId(int64_t id, int64_t userId);
    bool deleteByIdAndUserId(int64_t id, int64_t userId);

    bool updateStatusAndError(int64_t id,
                              int64_t userId,
                              const std::string& status,
                              const std::string& errorMessage = std::string{});

    bool updateGenerationResult(int64_t id,
                                int64_t userId,
                                const std::string& status,
                                const std::string& imageUrl,
                                const std::string& imageBase64,
                                const std::string& errorMessage,
                                double generationTime,
                                const std::optional<std::chrono::system_clock::time_point>& completedAt);

private:
    static void ensureTable();
};