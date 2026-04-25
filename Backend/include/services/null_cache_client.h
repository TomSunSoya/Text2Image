#pragma once
#include "services/i_cache_client.h"

namespace cache {

class NullCacheClient : public ICacheClient {
  public:
    bool isAvailable() const noexcept override {
        return false;
    }
    std::optional<std::string> get(std::string_view) const override {
        return std::nullopt;
    }
    bool setex(std::string_view, std::string_view, std::chrono::seconds) override {
        return false;
    }
    bool del(std::string_view) override {
        return false;
    }
    std::optional<int64_t> bumpVersion(std::string_view, std::string_view) override {
        return std::nullopt;
    }
    int64_t getVersion(std::string_view, std::string_view) const override {
        return 0;
    }
};
} // namespace cache
