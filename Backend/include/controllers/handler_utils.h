#pragma once

#include <expected>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "services/service_error.h"
#include "services/metrics_registry.h"
#include "utils/jwt_utils.h"

namespace controllers {

void fillServiceError(const drogon::HttpResponsePtr& resp, const ServiceError& error);
void fillDirectError(const drogon::HttpResponsePtr& resp, drogon::HttpStatusCode status,
                     std::string code, std::string message);
std::string clientIp(const drogon::HttpRequestPtr& req, bool trustProxy);
std::optional<utils::JwtPayload> resolveUser(const drogon::HttpRequestPtr& req,
                                             const drogon::HttpResponsePtr& resp);
std::optional<int64_t> resolveUserId(const drogon::HttpRequestPtr& req,
                                     const drogon::HttpResponsePtr& resp);

template <typename BodyFn>
void runJsonHandler(std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    std::string_view handlerName, BodyFn&& body) {
    const auto startedAt = std::chrono::steady_clock::now();
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    try {
        std::forward<BodyFn>(body)(resp);
    } catch (const nlohmann::json::parse_error& e) {
        spdlog::error("{}: json parse error: {}", handlerName, e.what());
        fillDirectError(resp, drogon::k400BadRequest, "invalid_json_body", "invalid json body");
    } catch (const std::exception& e) {
        spdlog::error("{}: error: {}", handlerName, e.what());
        fillDirectError(resp, drogon::k500InternalServerError, "internal_error", "internal error");
    }
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
    metrics::MetricsRegistry::instance().observeRequest(
        handlerName, static_cast<int>(resp->getStatusCode()), elapsed);
    callback(resp);
}

template <typename BodyFn>
void runAuthenticatedJson(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          std::string_view handlerName, BodyFn&& body) {
    runJsonHandler(std::move(callback), handlerName, [&](const drogon::HttpResponsePtr& resp) {
        const auto userId = resolveUserId(req, resp);
        if (!userId) {
            return;
        }
        body(*userId, resp);
    });
}

template <typename T, typename Renderer>
void respondFromExpected(const drogon::HttpResponsePtr& resp,
                         const std::expected<T, ServiceError>& result,
                         drogon::HttpStatusCode okStatus, Renderer&& renderer) {
    if (!result) {
        fillServiceError(resp, result.error());
        return;
    }
    resp->setStatusCode(okStatus);
    if constexpr (std::is_void_v<T>) {
        resp->setBody(std::forward<Renderer>(renderer)());
    } else {
        resp->setBody(std::forward<Renderer>(renderer)(*result));
    }
}

} // namespace controllers
