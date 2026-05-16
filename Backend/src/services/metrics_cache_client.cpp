#include "services/metrics_cache_client.h"

#include <stdexcept>
#include <utility>

namespace cache {

MetricsCacheClient::MetricsCacheClient(std::shared_ptr<ICacheClient> inner,
                                       std::shared_ptr<CacheMetrics> metrics)
    : inner_(std::move(inner)), metrics_(std::move(metrics)) {
    if (!inner_) {
        throw std::invalid_argument("Inner cache client cannot be null");
    }
    if (!metrics_) {
        throw std::invalid_argument("Cache metrics cannot be null");
    }
}

bool MetricsCacheClient::isAvailable() const noexcept {
    return inner_->isAvailable();
}
std::optional<std::string> MetricsCacheClient::get(std::string_view key) const {
    auto result = inner_->get(key);
    const auto ns = classifyKey(key);
    if (result) {
        metrics_->recordHit(ns);
    } else if (inner_->isAvailable()) {
        metrics_->recordMiss(ns);
    } else {
        metrics_->recordDegraded(ns);
    }
    return result;
}

bool MetricsCacheClient::setex(std::string_view key, std::string_view value,
                               std::chrono::seconds ttl) {
    return inner_->setex(key, value, ttl);
}

bool MetricsCacheClient::del(std::string_view key) {
    return inner_->del(key);
}

std::optional<int64_t> MetricsCacheClient::bumpVersion(std::string_view ns,
                                                       std::string_view identifier) {
    return inner_->bumpVersion(ns, identifier);
}

int64_t MetricsCacheClient::getVersion(std::string_view ns, std::string_view identifier) const {
    return inner_->getVersion(ns, identifier);
}

} // namespace cache