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
    switch (s) {
        case TaskStatus::Success:
        case TaskStatus::Failed:
        case TaskStatus::Cancelled:
        case TaskStatus::Timeout:
            return true;
        default:
            return false;
    }
}

constexpr bool canCancel(TaskStatus s) noexcept {
    switch (s) {
        case TaskStatus::Pending:
        case TaskStatus::Queued:
        case TaskStatus::Generating:
            return true;
        default:
            return false;
    }
}

constexpr bool canRetry(TaskStatus s, int retryCount, int maxRetries) noexcept {
    if (retryCount >= maxRetries)
        return false;
    switch (s) {
        case TaskStatus::Failed:
        case TaskStatus::Timeout:
        case TaskStatus::Cancelled:
            return true;
        default:
            return false;
    }
}

constexpr bool canDelete(TaskStatus s) noexcept {
    return isTerminal(s);
}

constexpr bool canReturnBinary(TaskStatus s, std::string_view storageKey) noexcept {
    return s == TaskStatus::Success && !storageKey.empty();
}

constexpr TaskStatus statusFromString(std::string_view s) noexcept {
    if (s == "pending")
        return TaskStatus::Pending;
    if (s == "queued")
        return TaskStatus::Queued;
    if (s == "generating")
        return TaskStatus::Generating;
    if (s == "success")
        return TaskStatus::Success;
    if (s == "failed")
        return TaskStatus::Failed;
    if (s == "cancelled")
        return TaskStatus::Cancelled;
    if (s == "timeout")
        return TaskStatus::Timeout;
    return TaskStatus::Unknown;
}

constexpr std::string_view statusToString(TaskStatus s) noexcept {
    switch (s) {
        case TaskStatus::Pending:
            return "pending";
        case TaskStatus::Queued:
            return "queued";
        case TaskStatus::Generating:
            return "generating";
        case TaskStatus::Success:
            return "success";
        case TaskStatus::Failed:
            return "failed";
        case TaskStatus::Cancelled:
            return "cancelled";
        case TaskStatus::Timeout:
            return "timeout";
        case TaskStatus::Unknown:
            return "unknown";
    }
    return "unknown"; // unreachable, but silences warnings
}

}
