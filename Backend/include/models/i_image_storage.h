#pragma once

#include <optional>
#include <string>

class IImageStorage {
  public:
    virtual ~IImageStorage() = default;

    [[nodiscard]] virtual std::optional<std::string>
    getBytes(const std::string& storageKey) const = 0;

    [[nodiscard]] virtual std::string presignUrl(const std::string& storageKey,
                                                 int expirySeconds = 0) const = 0;

    virtual bool remove(const std::string& storageKey) const = 0;

    [[nodiscard]] virtual std::string contentTypeForKey(const std::string& storageKey) const = 0;
};
