#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "models/image_generation.h"

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
