#pragma once

#include <optional>

#include <nlohmann/json.hpp>

#include "auth_service.h"
#include "image_generation.h"

struct ImageCreateResult {
    models::ImageGeneration generation;
};

class ImageService {
public:
    std::optional<ImageCreateResult> create(const nlohmann::json& payload, ServiceError& error) const;
};
