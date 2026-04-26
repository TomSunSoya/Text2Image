#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <format>

namespace image_cache {

constexpr std::string_view kNullMarker = "__NULL__";

namespace ttl {
constexpr std::chrono::seconds kMetaTerminal{300};
constexpr std::chrono::seconds kMetaInflight{5};
constexpr std::chrono::seconds kNullMarker{30};
} // namespace ttl

inline std::string metaKey(int64_t userId, int64_t id) {
    return std::format("img:meta:{}:{}", userId, id);
}
} // namespace image_cache