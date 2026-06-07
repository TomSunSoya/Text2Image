#pragma once

#include <memory>

#include "models/image_generation.h"
#include "services/i_http_client.h"
#include "services/image_service_types.h"

class GenerationClient {
  public:
    GenerationClient();
    explicit GenerationClient(std::shared_ptr<IHttpClient> httpClient);

    [[nodiscard]] models::ImageGeneration generate(models::ImageGeneration generation) const;
    [[nodiscard]] ImageHealthResult checkHealth() const;
    static void cleanupOrphanedStoredImage(const models::ImageGeneration& generation);

  private:
    std::shared_ptr<IHttpClient> httpClient_;
};
