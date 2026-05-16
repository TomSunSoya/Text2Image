#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

namespace cache {

enum class Namespace : uint8_t { Meta = 0, List = 1, Url = 2, Other = 3, _Count = 4 };

[[nodiscard]] Namespace classifyKey(std::string_view key) noexcept;
[[nodiscard]] std::string_view namespaceName(Namespace ns) noexcept;

class CacheMetrics {
  public:
    void recordHit(Namespace ns) noexcept;
    void recordMiss(Namespace ns) noexcept;
    void recordDegraded(Namespace ns) noexcept;

    struct Snapshot {
        uint64_t hits;
        uint64_t misses;
        uint64_t degraded;
    };
    [[nodiscard]] Snapshot snapshotFor(Namespace ns) const noexcept;
    [[nodiscard]] nlohmann::json toJson() const;

  private:
    struct Counters {
        std::atomic_uint64_t hits{0};
        std::atomic_uint64_t misses{0};
        std::atomic_uint64_t degraded{0};
    };
    std::array<Counters, static_cast<size_t>(Namespace::_Count)> counters_;
};
} // namespace cache
