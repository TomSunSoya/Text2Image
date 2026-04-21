#pragma once

#include "models/image_generation.h"
#include "services/image_service_types.h"

class GenerationClient {
  public:
    [[nodiscard]] static models::ImageGeneration generate(models::ImageGeneration generation);

    [[nodiscard]] static ImageHealthResult checkHealth();

    static void cleanupOrphanedStoredImage(const models::ImageGeneration& generation);
};
