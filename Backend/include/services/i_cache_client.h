#pragma once

#include <optional>
#include <string>
#include <chrono>
#include <string_view>
#include <cstdint>

namespace cache {

class ICacheClient {
  public:
    virtual ~ICacheClient() = default;

    [[nodiscard]] virtual bool isAvailable() const noexcept = 0;

    [[nodiscard]] virtual std::optional<std::string> get(std::string_view key) const = 0;

    virtual bool setex(std::string_view key, std::string_view value, std::chrono::seconds ttl) = 0;

    // Returns true only when a key existed and was removed.
    virtual bool del(std::string_view key) = 0;

    virtual std::optional<int64_t> bumpVersion(std::string_view ns,
                                               std::string_view identifier) = 0;

    virtual int64_t getVersion(std::string_view ns, std::string_view identifier) const = 0;
};
} // namespace cache
