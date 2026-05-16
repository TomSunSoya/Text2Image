#pragma once

#include <memory>

#include "services/i_cache_client.h"
#include "services/cache_metrics.h"

namespace cache {
class MetricsCacheClient : public ICacheClient {
  public:
    MetricsCacheClient(std::shared_ptr<ICacheClient> inner, std::shared_ptr<CacheMetrics> metrics);
    [[nodiscard]] bool isAvailable() const noexcept override;

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const override;

    bool setex(std::string_view key, std::string_view value, std::chrono::seconds ttl) override;
    bool del(std::string_view key) override;
    std::optional<int64_t> bumpVersion(std::string_view ns, std::string_view identifier) override;
    int64_t getVersion(std::string_view ns, std::string_view identifier) const override;

  private:
    std::shared_ptr<ICacheClient> inner_;
    std::shared_ptr<CacheMetrics> metrics_;
};
} // namespace cache