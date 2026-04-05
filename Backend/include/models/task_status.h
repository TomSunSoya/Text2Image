#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace models {

enum class TaskStatus : uint8_t {
    Pending,
    Queued,
    Generating,
    Success,
    Failed,
    Cancelled,
    Timeout,
    Unknown
};

constexpr bool isTerminal(TaskStatus s) noexcept {
    using enum TaskStatus;
    switch (s) {
        case Success:
        case Failed:
        case Cancelled:
        case Timeout:
            return true;
        default:
            return false;
    }
}

constexpr bool canCancel(TaskStatus s) noexcept {
    using enum TaskStatus;
    switch (s) {
        case Pending:
        case Queued:
        case Generating:
            return true;
        default:
            return false;
    }
}

constexpr bool canRetry(TaskStatus s, int retryCount, int maxRetries) noexcept {
    if (retryCount >= maxRetries)
        return false;
    using enum TaskStatus;
    switch (s) {
        case Failed:
        case Timeout:
        case Cancelled:
            return true;
        default:
            return false;
    }
}

constexpr bool canDelete(TaskStatus s) noexcept {
    return isTerminal(s);
}

constexpr bool canReturnBinary(TaskStatus s, std::string_view storageKey) noexcept {
    using enum TaskStatus;
    return s == Success && !storageKey.empty();
}

constexpr TaskStatus statusFromString(std::string_view s) noexcept {
    using enum TaskStatus;
    if (s == "pending")
        return Pending;
    if (s == "queued")
        return Queued;
    if (s == "generating")
        return Generating;
    if (s == "success")
        return Success;
    if (s == "failed")
        return Failed;
    if (s == "cancelled")
        return Cancelled;
    if (s == "timeout")
        return Timeout;
    return Unknown;
}

constexpr std::string_view statusToString(TaskStatus s) noexcept {
    using enum TaskStatus;
    switch (s) {
        case Pending:
            return "pending";
        case Queued:
            return "queued";
        case Generating:
            return "generating";
        case Success:
            return "success";
        case Failed:
            return "failed";
        case Cancelled:
            return "cancelled";
        case Timeout:
            return "timeout";
        case Unknown:
            return "unknown";
    }
    return "unknown"; // unreachable, but silences warnings
}

inline std::string statusToStdString(TaskStatus s) {
    return std::string(statusToString(s));
}

} // namespace models
