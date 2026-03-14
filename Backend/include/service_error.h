#pragma once

#include <string>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

struct ServiceError {
	drogon::HttpStatusCode status{ drogon::k500InternalServerError };
	std::string code{ "internal_error" };
	std::string message{ "internal_error" };
	nlohmann::json details = nlohmann::json::object();

	nlohmann::json toJson() const {
		nlohmann::json body = {
			{"code", code},
			{"message", message}
		};

		if (details.is_object() && !details.empty()) {
			body["details"] = details;
		}

		return body;
	}
};