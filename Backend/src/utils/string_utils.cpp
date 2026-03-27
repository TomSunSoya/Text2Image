#include "string_utils.h"

#include <cctype>

namespace utils {

std::optional<bool> parseBool(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isspace(byte)) {
            continue;
        }
        normalized += static_cast<char>(std::tolower(byte));
    }

    if (normalized == "1" || normalized == "true" || normalized == "yes" ||
        normalized == "on") {
        return true;
    }

    if (normalized == "0" || normalized == "false" || normalized == "no" ||
        normalized == "off") {
        return false;
    }

    return std::nullopt;
}

} // namespace utils
