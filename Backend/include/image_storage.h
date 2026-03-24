#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct StoredImage {
    std::string storage_key;
    std::string content_type{"image/png"};
};

class ImageStorage {
  public:
    StoredImage store(int64_t userId, const std::string& requestId, const std::string& rawBytes,
                      const std::string& contentType = "image/png") const;

    std::optional<std::string> getBytes(const std::string& storageKey) const;

    std::string presignUrl(const std::string& storageKey, int expirySeconds = 0) const;

    bool remove(const std::string& storageKey) const;

    std::string contentTypeForKey(const std::string& storageKey) const;
};
