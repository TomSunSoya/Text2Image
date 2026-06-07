#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include <random>
#include <string>
#include <string_view>

#include "models/task_status.h"

namespace image_cache {

constexpr std::string_view kNullMarker = "__NULL__";
constexpr std::string_view kListVersionNamespace = "img:list";
constexpr double kPresignCacheTtlRatio = 0.8;

namespace ttl {
constexpr std::chrono::seconds kMetaTerminal{300};
constexpr std::chrono::seconds kMetaInflight{5};
constexpr std::chrono::seconds kNullMarker{30};
constexpr std::chrono::seconds kListBase{60};
constexpr std::chrono::seconds kListJitterMax{10};
} // namespace ttl

inline std::string metaKey(int64_t userId, int64_t id) {
    return std::format("img:meta:{}:{}", userId, id);
}

inline std::string listMyKey(int64_t userId, int64_t version, int page, int size) {
    return std::format("img:list:my:{}:v{}:p{}:s{}", userId, version, page, size);
}

inline std::string listMyStatusKey(int64_t userId, int64_t version, models::TaskStatus status,
                                   int page, int size) {
    return std::format("img:list:status:{}:{}:v{}:p{}:s{}", userId, models::statusToString(status),
                       version, page, size);
}

inline int normalizePage(int p) noexcept {
    return p < 0 ? 0 : p;
}

inline int normalizeSize(int s) noexcept {
    return s <= 0 ? 10 : s;
}

inline std::chrono::seconds listTtlWithJitter() {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::chrono::seconds::rep> dist(0, ttl::kListJitterMax.count());
    return ttl::kListBase + std::chrono::seconds(dist(rng));
}

inline std::string presignKey(std::string_view storageKey) {
    return std::format("img:url:{}", storageKey);
}

inline std::chrono::seconds derivePresignTtl(std::chrono::seconds minioExpiry) {
    if (minioExpiry <= std::chrono::seconds(0)) {
        return std::chrono::seconds(0);
    }

    const auto seconds = static_cast<std::chrono::seconds::rep>(
        static_cast<double>(minioExpiry.count()) * kPresignCacheTtlRatio);
    return std::chrono::seconds{(std::max)(std::chrono::seconds::rep{1}, seconds)};
}
} // namespace image_cache
