#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "models/i_image_storage.h"

struct StoredImage {
    std::string storage_key;
    std::string content_type{"image/png"};
};

class ImageStorage : public IImageStorage {
  public:
    StoredImage store(int64_t userId, const std::string& requestId, const std::string& rawBytes,
                      const std::string& contentType = "image/png") const;

    [[nodiscard]] std::optional<std::string>
    getBytes(const std::string& storageKey) const override;

    [[nodiscard]] std::string presignUrl(const std::string& storageKey,
                                         int expirySeconds = 0) const override;

    bool remove(const std::string& storageKey) const override;

    [[nodiscard]] std::string contentTypeForKey(const std::string& storageKey) const override;
};
