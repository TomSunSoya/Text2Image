#include "utils/string_utils.h"

#include <cctype>
#include <ranges>

namespace utils {

std::optional<bool> parseBool(std::string value) {
    auto normalized = value
        | std::views::filter([](unsigned char c) { return !std::isspace(c); })
        | std::views::transform([](unsigned char c) { return static_cast<char>(std::tolower(c)); })
        | std::ranges::to<std::string>();

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
