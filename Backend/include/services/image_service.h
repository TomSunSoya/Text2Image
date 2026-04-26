#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "database/i_image_repo.h"
#include "models/i_image_storage.h"
#include "services/cache_client.h"
#include "services/generation_client.h"
#include "services/image_service_types.h"
#include "services/service_error.h"

class ImageService {
  public:
    ImageService();
    ImageService(std::shared_ptr<IImageRepo> repo, std::shared_ptr<IImageStorage> storage);
    ImageService(std::shared_ptr<IImageRepo> repo, std::shared_ptr<IImageStorage> storage,
                 std::shared_ptr<cache::ICacheClient> cache);

    static void bootstrapWorkers(std::shared_ptr<cache::ICacheClient> cache = nullptr);
    static void setDefaultCache(std::shared_ptr<cache::ICacheClient> cache);

    [[nodiscard]] std::expected<ImageCreateResult, ServiceError>
    create(int64_t userId, const nlohmann::json& payload) const;

    [[nodiscard]] std::expected<ImageListResult, ServiceError> listMy(int64_t userId, int page,
                                                                      int size) const;
    [[nodiscard]] std::expected<ImageListResult, ServiceError>
    listMyByStatus(int64_t userId, const std::string& status, int page, int size) const;

    [[nodiscard]] std::expected<ImageGetResult, ServiceError>
    getById(int64_t userId, int64_t id, bool includeImagePayload = true) const;
    [[nodiscard]] std::expected<ImageGetResult, ServiceError> cancelById(int64_t userId,
                                                                         int64_t id) const;
    [[nodiscard]] std::expected<ImageGetResult, ServiceError> retryById(int64_t userId,
                                                                        int64_t id) const;
    [[nodiscard]] std::expected<ImageBinaryResult, ServiceError> getBinaryById(int64_t userId,
                                                                               int64_t id) const;

    [[nodiscard]] std::expected<void, ServiceError> deleteById(int64_t userId, int64_t id) const;

    [[nodiscard]] ImageHealthResult checkHealth() const;

  private:
    std::shared_ptr<IImageRepo> repo_;
    std::shared_ptr<IImageStorage> storage_;
    std::shared_ptr<cache::ICacheClient> cache_;
    GenerationClient generation_client_;

    void writeToCache(const std::string& key, const models::ImageGeneration& image) const;
    void presignInPlace(models::ImageGeneration& image) const;
};
