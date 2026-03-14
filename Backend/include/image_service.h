#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "auth_service.h"
#include "image_generation.h"

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
    std::string content_type{ "image/png" };
};

struct ImageHealthResult {
    std::string status{"unhealthy"};
    bool model_loaded{false};
    std::string detail;
};

class ImageService {
public:
	static void bootstrapWorkers();

    std::optional<ImageCreateResult> create(int64_t userId,
                                            const nlohmann::json& payload,
                                            ServiceError& error) const;

    std::optional<ImageListResult> listMy(int64_t userId, int page, int size, ServiceError& error) const;
    std::optional<ImageListResult> listMyByStatus(int64_t userId,
                                                  const std::string& status,
                                                  int page,
                                                  int size,
                                                  ServiceError& error) const;

    std::optional<ImageGetResult> getById(int64_t userId, int64_t id, ServiceError& error, bool includeImagePayload = true) const;
	std::optional<ImageGetResult> cancelById(int64_t userId, int64_t id, ServiceError& error) const;
	std::optional<ImageGetResult> retryById(int64_t userId, int64_t id, ServiceError& error) const;
	std::optional<ImageBinaryResult> getBinaryById(int64_t userId, int64_t id, ServiceError& error) const;

    bool deleteById(int64_t userId, int64_t id, ServiceError& error) const;

    ImageHealthResult checkHealth() const;
};
