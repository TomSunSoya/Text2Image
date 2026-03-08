#pragma once

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

private:
    static void ensureTable();
};
