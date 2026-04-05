# ZImage Backend — C++23 现代化改造指南

逐个落地的代码改造方案，按优先级排序。每项改造都是独立的，可以单独合入。

---

## 改造 1：TaskStatus 强类型枚举（影响面最大，收益最高）

### 问题

当前 `status` 全程是 `std::string`，`"success"` / `"failed"` 等魔法字符串散落在 5+ 个文件中，拼错不会报编译错误。`isTerminal()` 在 `task_state_machine.cpp`、`ImageRepo.cpp`、`ImageGeneration::isTerminal()` 三处重复实现。

### 新增文件：`include/models/task_status.h`

```cpp
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
    if (retryCount >= maxRetries) return false;
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

// 数据库/JSON 读入时调用一次，之后全程类型安全
constexpr TaskStatus statusFromString(std::string_view s) noexcept {
    if (s == "pending")    return TaskStatus::Pending;
    if (s == "queued")     return TaskStatus::Queued;
    if (s == "generating") return TaskStatus::Generating;
    if (s == "success")    return TaskStatus::Success;
    if (s == "failed")     return TaskStatus::Failed;
    if (s == "cancelled")  return TaskStatus::Cancelled;
    if (s == "timeout")    return TaskStatus::Timeout;
    return TaskStatus::Unknown;
}

constexpr std::string_view statusToString(TaskStatus s) noexcept {
    switch (s) {
        case TaskStatus::Pending:    return "pending";
        case TaskStatus::Queued:     return "queued";
        case TaskStatus::Generating: return "generating";
        case TaskStatus::Success:    return "success";
        case TaskStatus::Failed:     return "failed";
        case TaskStatus::Cancelled:  return "cancelled";
        case TaskStatus::Timeout:    return "timeout";
        case TaskStatus::Unknown:    return "unknown";
    }
    return "unknown"; // unreachable, but silences warnings
}

} // namespace models
```

### 改造 `ImageGeneration` model

```cpp
// image_generation.h — 把 std::string status 改为枚举
// Before:
std::string status{"pending"};

// After:
TaskStatus status{TaskStatus::Pending};
```

### 改造使用方

```cpp
// Before (image_service.cpp):
generation.status = "queued";
if (generation.status == "success" || generation.status == "failed") { ... }

// After:
generation.status = TaskStatus::Queued;
if (isTerminal(generation.status)) { ... }
```

```cpp
// Before (image_service.cpp — normalizeStatus):
if (status == "success" || status == "succeeded" || ...) return "success";

// After: normalizeStatus 仍返回 TaskStatus，但类型安全
TaskStatus normalizeStatus(std::string_view rawStatus) {
    const auto lower = toLower(std::string{rawStatus});
    if (lower == "success" || lower == "succeeded" || lower == "completed"
        || lower == "done" || lower == "ok")
        return TaskStatus::Success;
    if (lower == "failed" || lower == "failure" || lower == "error")
        return TaskStatus::Failed;
    if (lower == "queued" || lower == "pending" || lower == "processing"
        || lower == "generating")
        return TaskStatus::Queued;
    return statusFromString(lower);
}
```

```cpp
// toJson() 里序列化:
j["status"] = std::string(statusToString(status));

// fromJson() 里反序列化:
if (const auto value = readStringAny(j, {"status"}))
    img.status = statusFromString(*value);

// ImageRepo rowToImageGeneration:
image.status = models::statusFromString(getStringOrEmpty(row, 9));
```

### 可以删除的代码

- `utils/task_state_machine.h` + `.cpp` 整个文件（逻辑合并进 `task_status.h`）
- `ImageRepo.cpp` 的 `isTerminalStatus()` 局部函数
- `ImageGeneration::isTerminal()` 改为 `bool isTerminal() const { return models::isTerminal(status); }`

---

## 改造 2：`std::chrono` + `std::format` 替代手写时间解析

### 问题

`timeToDbString()` 和 `parseDbTime()` 在 `ImageRepo.cpp` 和 `image_generation.cpp` 各有一份，手动操作 `std::tm`，跨平台还要 `#ifdef _WIN32`。

### 新增公共工具：`include/utils/chrono_utils.h`

```cpp
#pragma once

#include <chrono>
#include <format>
#include <optional>
#include <sstream>
#include <string>

namespace utils::chrono {

// std::format 直接格式化 chrono — 消除所有 localtime_s/localtime_r 分支
inline std::string toDbString(const std::chrono::system_clock::time_point& tp) {
    // floor 到秒精度，与 MySQL DATETIME 对齐
    return std::format("{:%Y-%m-%d %H:%M:%S}",
                       std::chrono::floor<std::chrono::seconds>(tp));
}

// 从 DB 字符串解析（保留容错逻辑）
inline std::optional<std::chrono::system_clock::time_point>
fromDbString(std::string_view value) {
    if (value.empty()) return std::nullopt;

    // 截掉小数秒和 T 分隔符
    std::string cleaned{value};
    if (auto dot = cleaned.find('.'); dot != std::string::npos)
        cleaned.resize(dot);
    for (auto& ch : cleaned)
        if (ch == 'T') ch = ' ';

    std::tm tm{};
    std::istringstream iss(cleaned);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) return std::nullopt;

    const auto t = std::mktime(&tm);
    if (t == -1) return std::nullopt;
    return std::chrono::system_clock::from_time_t(t);
}

// 注: 等 MSVC/GCC 都完整支持 std::chrono::parse (C++26 / 部分 C++20)
// 后可以进一步简化 fromDbString 为:
//   std::chrono::sys_seconds tp;
//   std::istringstream iss(cleaned);
//   std::chrono::from_stream(iss, "%Y-%m-%d %H:%M:%S", tp);

} // namespace utils::chrono
```

### 改造点

```cpp
// ImageRepo.cpp — 删除局部 timeToDbString / parseDbTime，改为:
#include "utils/chrono_utils.h"

// 所有调用点:
// Before: timeToDbString(createdAt)
// After:  utils::chrono::toDbString(createdAt)

// Before: parseDbTime(getStringOrEmpty(row, 19))
// After:  utils::chrono::fromDbString(getStringOrEmpty(row, 19))
```

```cpp
// image_generation.cpp — 删除局部 timeToString，改为:
#include "utils/chrono_utils.h"

static void putOptionalTime(nlohmann::json& j, const char* key,
                            const std::optional<std::chrono::system_clock::time_point>& value) {
    if (value) j[key] = utils::chrono::toDbString(*value);
}
```

**结果**: 消除 2 份重复实现 + 所有 `#ifdef _WIN32` 分支（`std::format` 对 chrono 是跨平台的）。

---

## 改造 3：`std::ranges` 简化集合操作

### 3a. `toListJson` — controller 层

```cpp
// Before (image_controller.cpp):
nlohmann::json toListJson(const ImageListResult& result) {
    nlohmann::json content = nlohmann::json::array();
    for (const auto& item : result.content) {
        content.push_back(item.toJson());
    }
    return {{"content", content}, {"totalElements", result.total_elements}};
}

// After:
#include <ranges>

nlohmann::json toListJson(const ImageListResult& result) {
    auto content = result.content
        | std::views::transform(&models::ImageGeneration::toJson);
    return {
        {"content", nlohmann::json(std::vector(content.begin(), content.end()))},
        {"totalElements", result.total_elements}
    };
}
// 注：nlohmann::json 不直接接受 range，所以需要 std::vector 中转。
// 如果觉得啰嗦，也可以写：
// nlohmann::json arr = nlohmann::json::array();
// std::ranges::for_each(result.content, [&](const auto& item) {
//     arr.push_back(item.toJson());
// });
```

### 3b. `presignListImages` — 用 ranges::filter + for_each

```cpp
// Before (image_service.cpp):
for (auto& img : images) {
    if (!img.storage_key.empty()) {
        try {
            img.image_url = storage.presignUrl(img.storage_key);
        } catch (...) { ... }
    }
}

// After:
for (auto& img : images | std::views::filter([](const auto& i) {
         return !i.storage_key.empty();
     })) {
    try {
        img.image_url = storage.presignUrl(img.storage_key);
    } catch (const std::exception& ex) {
        spdlog::error("presignListImages: failed for key='{}': {}",
                      img.storage_key, ex.what());
    }
}
```

### 3c. `copySubscribersLocked` — TaskEventHub

```cpp
// Before (task_event_hub.cpp):
std::vector<drogon::WebSocketConnectionPtr>
TaskEventHub::copySubscribersLocked(int64_t userId) const {
    std::lock_guard lock(mutex_);
    std::vector<drogon::WebSocketConnectionPtr> subscribers;
    const auto it = subscribers_.find(userId);
    if (it == subscribers_.end()) return subscribers;
    subscribers.reserve(it->second.size());
    for (const auto& [_, connection] : it->second) {
        if (connection) subscribers.push_back(connection);
    }
    return subscribers;
}

// After:
std::vector<drogon::WebSocketConnectionPtr>
TaskEventHub::copySubscribersLocked(int64_t userId) const {
    std::lock_guard lock(mutex_);
    const auto it = subscribers_.find(userId);
    if (it == subscribers_.end()) return {};

    auto valid = it->second | std::views::values
               | std::views::filter([](const auto& conn) { return conn != nullptr; });
    return {valid.begin(), valid.end()};
}
```

### 3d. `toLower` — 用 `std::ranges::transform`

```cpp
// Before (image_service.cpp):
std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// After:
std::string toLower(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
```

### 3e. `trimInPlace` — 用 ranges 算法

```cpp
// Before:
void trimInPlace(std::string& s) {
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
    s.erase(
        std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
        s.end());
}

// After:
void trimInPlace(std::string& s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::ranges::find_if(s, notSpace));
    s.erase(std::ranges::find_if(s | std::views::reverse, notSpace).base(), s.end());
}
```

---

## 改造 4：`std::optional` 的 monadic 操作（C++23）

C++23 给 `std::optional` 加了 `and_then` / `transform` / `or_else`，可以链式处理。

### 4a. `resolveUserId` — controller 层

```cpp
// Before (image_controller.cpp):
const auto token = utils::extractBearerToken(req);
if (!token) {
    fillDirectError(resp, drogon::k401Unauthorized, "missing_bearer_token", "...");
    return std::nullopt;
}
const auto payload = utils::verifyToken(*token);
if (!payload || payload->user_id <= 0) {
    fillDirectError(resp, drogon::k401Unauthorized, "invalid_token", "...");
    return std::nullopt;
}
return payload->user_id;

// After:
return utils::extractBearerToken(req)
    .and_then([](const std::string& token) { return utils::verifyToken(token); })
    .and_then([](const auto& payload) -> std::optional<int64_t> {
        return payload.user_id > 0
            ? std::optional{payload.user_id}
            : std::nullopt;
    })
    .or_else([&]() -> std::optional<int64_t> {
        fillDirectError(resp, drogon::k401Unauthorized, "unauthorized", "unauthorized");
        return std::nullopt;
    });
```

### 4b. `getStringField` + URL 回退逻辑

```cpp
// Before (image_service.cpp — mergeRemoteResult):
if (generation.image_url.empty()) {
    if (const auto imageUrl = getStringField(*payload, "image_url")) {
        generation.image_url = *imageUrl;
    } else if (const auto url = getStringField(*payload, "url")) {
        generation.image_url = *url;
    }
}

// After:
if (generation.image_url.empty()) {
    generation.image_url = getStringField(*payload, "image_url")
        .or_else([&]() { return getStringField(*payload, "url"); })
        .value_or(std::string{});
}
```

---

## 改造 5：`[[nodiscard]]` 强化返回值检查

对所有返回 `std::expected` / `std::optional` / `bool` 的关键函数加 `[[nodiscard]]`，防止忽略返回值。

```cpp
// image_service.h:
class ImageService {
  public:
    static void bootstrapWorkers();

    [[nodiscard]] std::expected<ImageCreateResult, ServiceError>
    create(int64_t userId, const nlohmann::json& payload) const;

    [[nodiscard]] std::expected<ImageListResult, ServiceError>
    listMy(int64_t userId, int page, int size) const;

    // ... 所有 std::expected 返回值都加上
};

// service_error.h:
struct ServiceError {
    // ...
    [[nodiscard]] nlohmann::json toJson() const;
};

// ImageRepo.h:
class ImageRepo {
    [[nodiscard]] std::optional<models::ImageGeneration>
    findByIdAndUserId(int64_t id, int64_t userId);
    // ...
};
```

---

## 改造 6：`using enum` 简化 switch（C++20，搭配改造 1）

引入 `TaskStatus` 枚举后，switch 语句可以用 `using enum` 减少前缀噪音：

```cpp
// Before:
switch (s) {
    case TaskStatus::Success:
    case TaskStatus::Failed:
    case TaskStatus::Cancelled:
    case TaskStatus::Timeout:
        return true;
    default:
        return false;
}

// After:
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
```

---

## 改造 7：`std::from_chars` 替代 `std::stoi`

`std::stoi` 要构造异常，还依赖 locale。`std::from_chars` 是零分配、无异常、无 locale 的替代。

```cpp
// Before (image_controller.cpp):
int parsePositiveInt(const std::string& value, int fallback) {
    if (value.empty()) return fallback;
    try {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

// After:
#include <charconv>

int parsePositiveInt(std::string_view value, int fallback) {
    int parsed{};
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return (ec == std::errc{} && parsed > 0) ? parsed : fallback;
}
```

---

## 改造 8：FailureCode 强类型（可选，搭配改造 1）

与 `TaskStatus` 类似，`failure_code` 目前也是魔法字符串。如果觉得枚举太多可以暂缓，但至少可以抽常量：

```cpp
// include/models/failure_code.h
#pragma once
#include <string_view>

namespace models::failure {

inline constexpr std::string_view kPythonServiceRequestFailed  = "python_service_request_failed";
inline constexpr std::string_view kPythonServiceEmptyResponse  = "python_service_empty_response";
inline constexpr std::string_view kPythonServiceInvalidJson    = "python_service_invalid_json";
inline constexpr std::string_view kPythonServiceException      = "python_service_exception";
inline constexpr std::string_view kPythonServiceUnknown        = "python_service_unknown_exception";
inline constexpr std::string_view kMissingImagePayload         = "missing_image_payload";
inline constexpr std::string_view kStorageWriteFailed          = "storage_write_failed";

} // namespace models::failure
```

```cpp
// 使用方:
// Before:
generation.failure_code = "python_service_request_failed";

// After:
generation.failure_code = std::string(models::failure::kPythonServiceRequestFailed);
```

---

## 改造总结

| # | 改造项 | 涉及特性 | 风险 | 收益 |
|---|--------|----------|------|------|
| 1 | TaskStatus 枚举 | C++20 enum class, constexpr | 中 | ★★★★★ 消除魔法字符串，编译期检查 |
| 2 | chrono 时间工具 | C++20 std::format chrono | 低 | ★★★★ 消除重复代码 + 跨平台 ifdef |
| 3 | ranges 管道 | C++20 ranges | 低 | ★★★ 更声明式，更易读 |
| 4 | optional monadic | C++23 and_then/or_else | 低 | ★★★ 减少嵌套 if |
| 5 | [[nodiscard]] | C++17 attribute | 无 | ★★★ 零成本防御 |
| 6 | using enum | C++20 | 无 | ★★ 减少 switch 噪音 |
| 7 | std::from_chars | C++17 | 无 | ★★ 无异常、无分配 |
| 8 | FailureCode 常量 | C++17 inline constexpr | 无 | ★★ 防拼错 |

**建议落地顺序**: 5 → 7 → 2 → 6 → 3 → 8 → 1 → 4

（从零风险的 `[[nodiscard]]` 和 `from_chars` 开始，逐步到需要改 model 结构的 `TaskStatus` 枚举，最后做 `optional` monadic 链。）
