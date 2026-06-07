#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <drogon/HttpRequest.h>
#include <nlohmann/json.hpp>

namespace audit {

struct Event {
    std::string event;
    std::string outcome;
    std::string request_id;
    std::string client_ip;
    std::optional<std::string> resource_id;
    std::optional<int64_t> user_id;
    int status_code{0};
};

nlohmann::json toJson(const Event& event);
std::string safeLogValue(std::string_view value);
std::string requestIdFrom(const drogon::HttpRequestPtr& req);

void logHttpEvent(const drogon::HttpRequestPtr& req, std::string_view event,
                  std::string_view outcome, std::optional<int64_t> userId, int statusCode,
                  std::string_view clientIp, std::string_view resourceId = {});

} // namespace audit
