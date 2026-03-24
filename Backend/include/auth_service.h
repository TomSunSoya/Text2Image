#pragma once

#include <expected>
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
    std::expected<RegisterResult, ServiceError> registerUser(const nlohmann::json& payload) const;
    std::expected<LoginResult, ServiceError> login(const nlohmann::json& payload) const;
};
