#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "image_generation.h"
#include "service_error.h"

struct ImageCreateResult {
    models::ImageGeneration generation;
};

struct ImageListResult {
    std::vector<models::ImageGeneration> content;
    int64_t total_elements{0};
};

struct ImageGetResult {
    models::ImageGeneration generation;
};

struct ImageBinaryResult {
    std::string body;
    std::string content_type{"image/png"};
};

struct ImageHealthResult {
    std::string status{"unhealthy"};
    bool model_loaded{false};
    std::string detail;
};

class ImageService {
  public:
    static void bootstrapWorkers();

    std::expected<ImageCreateResult, ServiceError> create(int64_t userId,
                                                          const nlohmann::json& payload) const;

    std::expected<ImageListResult, ServiceError> listMy(int64_t userId, int page, int size) const;
    std::expected<ImageListResult, ServiceError>
    listMyByStatus(int64_t userId, const std::string& status, int page, int size) const;

    std::expected<ImageGetResult, ServiceError> getById(int64_t userId, int64_t id,
                                                        bool includeImagePayload = true) const;
    std::expected<ImageGetResult, ServiceError> cancelById(int64_t userId, int64_t id) const;
    std::expected<ImageGetResult, ServiceError> retryById(int64_t userId, int64_t id) const;
    std::expected<ImageBinaryResult, ServiceError> getBinaryById(int64_t userId, int64_t id) const;

    std::expected<void, ServiceError> deleteById(int64_t userId, int64_t id) const;

    ImageHealthResult checkHealth() const;
};
