#include "utils/request_id.h"

#include <cstdint>
#include <format>
#include <random>

namespace utils {

namespace {

bool isAllowedChar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == ':' || c == '-';
}

constexpr std::size_t kMaxRequestIdLength = 128;

} // namespace

std::string generateRequestId() {
    thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<std::uint64_t> dist;

    std::uint64_t hi = dist(rng);
    std::uint64_t lo = dist(rng);
    // Stamp the UUIDv4 version (4) and variant (10xx) bits.
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    return std::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}", static_cast<std::uint32_t>(hi >> 32),
                       static_cast<std::uint32_t>((hi >> 16) & 0xFFFF),
                       static_cast<std::uint32_t>(hi & 0xFFFF),
                       static_cast<std::uint32_t>(lo >> 48), lo & 0xFFFFFFFFFFFFULL);
}

std::string sanitizeRequestId(std::string_view raw) {
    std::size_t begin = 0;
    std::size_t end = raw.size();
    while (begin < end && static_cast<unsigned char>(raw[begin]) <= ' ') {
        ++begin;
    }
    while (end > begin && static_cast<unsigned char>(raw[end - 1]) <= ' ') {
        --end;
    }

    const std::string_view trimmed = raw.substr(begin, end - begin);
    if (trimmed.empty() || trimmed.size() > kMaxRequestIdLength) {
        return {};
    }

    for (const char ch : trimmed) {
        if (!isAllowedChar(static_cast<unsigned char>(ch))) {
            return {};
        }
    }

    return std::string{trimmed};
}

} // namespace utils
