#include "services/cache_metrics.h"

#include <nlohmann/json.hpp>

namespace cache {
Namespace classifyKey(std::string_view key) noexcept {
    if (key.starts_with("img:meta:"))
        return Namespace::Meta;
    if (key.starts_with("img:list:"))
        return Namespace::List;
    if (key.starts_with("img:url:"))
        return Namespace::Url;
    if (key.starts_with("ver:img:list:"))
        return Namespace::List;
    return Namespace::Other;
}

std::string_view namespaceName(Namespace ns) noexcept {
    switch (ns) {
        case Namespace::Meta:
            return "meta";
        case Namespace::List:
            return "list";
        case Namespace::Url:
            return "url";
        case Namespace::Other:
            return "other";
        default:
            return "unknown";
    }
}

void CacheMetrics::recordHit(Namespace ns) noexcept {
    counters_[static_cast<size_t>(ns)].hits.fetch_add(1, std::memory_order_relaxed);
}

void CacheMetrics::recordMiss(Namespace ns) noexcept {
    counters_[static_cast<size_t>(ns)].misses.fetch_add(1, std::memory_order_relaxed);
}

void CacheMetrics::recordDegraded(Namespace ns) noexcept {
    counters_[static_cast<size_t>(ns)].degraded.fetch_add(1, std::memory_order_relaxed);
}

CacheMetrics::Snapshot CacheMetrics::snapshotFor(Namespace ns) const noexcept {
    const auto& c = counters_[static_cast<size_t>(ns)];
    return {c.hits.load(std::memory_order_relaxed), c.misses.load(std::memory_order_relaxed),
            c.degraded.load(std::memory_order_relaxed)};
}

nlohmann::json CacheMetrics::toJson() const {
    nlohmann::json j;
    j["namespaces"] = nlohmann::json::object();
    for (size_t i = 0; i < static_cast<size_t>(Namespace::_Count); ++i) {
        const auto ns = static_cast<Namespace>(i);
        const auto snap = snapshotFor(ns);
        j["namespaces"][std::string(namespaceName(ns))] = {
            {"hits", snap.hits}, {"misses", snap.misses}, {"degraded", snap.degraded}};
    }

    return j;
}
} // namespace cache