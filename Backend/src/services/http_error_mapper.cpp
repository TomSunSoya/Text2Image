#include "services/http_error_mapper.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace {

nlohmann::json makeDetails(const HttpError& error) {
    nlohmann::json details = {{"status_code", error.status_code}};

    if (!error.message.empty()) {
        details["reason"] = error.message;
    }

    return details;
}

} // namespace

ServiceError mapHttpError(const HttpError& error) {
    spdlog::warn("model service HTTP error status={}, message={}", error.status_code,
                 error.message);

    if (error.status_code == 0) {
        return ServiceError{drogon::k503ServiceUnavailable, "model_service_unavailable",
                            "model service is unavailable", makeDetails(error)};
    }

    if (error.status_code >= 500) {
        return ServiceError{drogon::k502BadGateway, "model_service_error",
                            "model service returned an error", makeDetails(error)};
    }

    if (error.status_code >= 400) {
        return ServiceError{drogon::k502BadGateway, "model_service_error",
                            "model service rejected the request", makeDetails(error)};
    }

    return ServiceError{drogon::k502BadGateway, "model_service_error",
                        "model service request failed", makeDetails(error)};
}
