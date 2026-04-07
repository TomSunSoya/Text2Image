#pragma once

#include <string>
#include <utility>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

struct ServiceError {
    drogon::HttpStatusCode status{drogon::k500InternalServerError};
    std::string code{"internal_error"};
    std::string message{"internal_error"};
    nlohmann::json details = nlohmann::json::object();

    ServiceError() = default;

    ServiceError(drogon::HttpStatusCode s, std::string c, std::string m)
        : status(s), code(std::move(c)), message(std::move(m)) {}

    ServiceError(drogon::HttpStatusCode s, std::string c, std::string m, nlohmann::json d)
        : status(s), code(std::move(c)), message(std::move(m)), details(std::move(d)) {}

    [[nodiscard]] nlohmann::json toJson() const {
        nlohmann::json body = {{"code", code}, {"message", message}};

        if (details.is_object() && !details.empty()) {
            body["details"] = details;
        }

        return body;
    }
};
