#include "utils/audit_log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <spdlog/spdlog.h>

#include "utils/request_id.h"

namespace audit {

namespace {

constexpr std::size_t kMaxLogValueLength = 128;

bool isSafeVisibleChar(unsigned char ch) {
    return ch >= 0x20 && ch < 0x7F;
}

std::string nowIsoUtc() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif

    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

} // namespace

std::string safeLogValue(std::string_view value) {
    std::string sanitized;
    sanitized.reserve((std::min)(value.size(), kMaxLogValueLength));
    for (const char ch : value) {
        if (sanitized.size() >= kMaxLogValueLength) {
            break;
        }
        const auto byte = static_cast<unsigned char>(ch);
        sanitized.push_back(isSafeVisibleChar(byte) ? ch : '_');
    }
    return sanitized.empty() ? "-" : sanitized;
}

std::string requestIdFrom(const drogon::HttpRequestPtr& req) {
    if (!req) {
        return "-";
    }

    const std::string key{utils::kRequestIdAttribute};
    if (const auto attrs = req->attributes(); attrs && attrs->find(key)) {
        try {
            return safeLogValue(attrs->get<std::string>(key));
        } catch (...) {
        }
    }

    const auto header =
        utils::sanitizeRequestId(req->getHeader(std::string(utils::kRequestIdHeader)));
    return header.empty() ? "-" : safeLogValue(header);
}

nlohmann::json toJson(const Event& event) {
    nlohmann::json body = {
        {"type", "audit"},
        {"timestamp", nowIsoUtc()},
        {"event", safeLogValue(event.event)},
        {"outcome", safeLogValue(event.outcome)},
        {"request_id", safeLogValue(event.request_id)},
        {"client_ip", safeLogValue(event.client_ip)},
        {"status", event.status_code},
    };

    if (event.user_id.has_value()) {
        body["user_id"] = *event.user_id;
    }
    if (event.resource_id.has_value()) {
        body["resource_id"] = safeLogValue(*event.resource_id);
    }

    return body;
}

void logHttpEvent(const drogon::HttpRequestPtr& req, std::string_view event,
                  std::string_view outcome, std::optional<int64_t> userId, int statusCode,
                  std::string_view clientIp, std::string_view resourceId) {
    const Event auditEvent{
        std::string(event),
        std::string(outcome),
        requestIdFrom(req),
        safeLogValue(clientIp),
        resourceId.empty() ? std::nullopt : std::optional<std::string>{safeLogValue(resourceId)},
        userId,
        statusCode};
    spdlog::info("audit {}", toJson(auditEvent).dump());
}

} // namespace audit
