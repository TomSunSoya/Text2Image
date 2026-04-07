#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace utils::chrono {

inline std::string toDbString(const std::chrono::system_clock::time_point& tp) {
    const auto tt =
        std::chrono::system_clock::to_time_t(std::chrono::floor<std::chrono::seconds>(tp));
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

inline std::optional<std::chrono::system_clock::time_point> fromDbString(std::string_view value) {
    if (value.empty())
        return std::nullopt;

    std::string cleaned{value};
    if (const auto dot = cleaned.find('.'); dot != std::string::npos)
        cleaned.resize(dot);
    for (auto& ch : cleaned)
        if (ch == 'T')
            ch = ' ';

    std::tm tm{};
    std::istringstream iss(cleaned);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail())
        return std::nullopt;

    // timegm 把 tm 当 UTC 解析，与 toDbString 的 UTC 输出对称
#ifdef _WIN32
    const auto t = _mkgmtime(&tm);
#else
    const auto t = timegm(&tm);
#endif
    if (t == -1)
        return std::nullopt;
    return std::chrono::system_clock::from_time_t(t);
}

} // namespace utils::chrono
