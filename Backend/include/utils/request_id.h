#pragma once

#include <string>
#include <string_view>

namespace utils {

// Header and request-attribute keys for the cross-service trace id.
inline constexpr std::string_view kRequestIdHeader = "X-Request-Id";
inline constexpr std::string_view kRequestIdAttribute = "requestId";

// Generates a fresh UUIDv4-style trace id.
std::string generateRequestId();

// Validates a client-supplied trace id. Returns the trimmed value when it is a
// safe token ([A-Za-z0-9._:-], 1..128 chars), or an empty string otherwise so
// the caller falls back to a generated id. Guards against header/log injection.
std::string sanitizeRequestId(std::string_view raw);

} // namespace utils
