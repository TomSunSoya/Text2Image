#include "utils/string_utils.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <ranges>

namespace utils {

std::optional<bool> parseBool(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    auto chars =
        value | std::views::filter([](unsigned char c) { return !std::isspace(c); }) |
        std::views::transform([](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::ranges::copy(chars, std::back_inserter(normalized));

    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }

    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }

    return std::nullopt;
}

} // namespace utils
