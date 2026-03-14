#pragma once

#include <optional>
#include <string>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

#include "user.h"
#include "service_error.h"

struct RegisterResult {
    models::User user;
};

struct LoginResult {
    models::User user;
    std::string token;
};

class AuthService {
public:
    std::optional<RegisterResult> registerUser(const nlohmann::json& payload, ServiceError& error) const;
    std::optional<LoginResult> login(const nlohmann::json& payload, ServiceError& error) const;
};
