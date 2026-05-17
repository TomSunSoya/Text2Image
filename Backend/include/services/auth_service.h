#pragma once

#include <expected>
#include <memory>
#include <string>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

#include "database/i_user_repo.h"
#include "models/user.h"
#include "services/service_error.h"

struct RegisterResult {
    models::User user;
};

struct LoginResult {
    models::User user;
    std::string token;
};

class AuthService {
  public:
    AuthService();
    explicit AuthService(std::shared_ptr<IUserRepo> repo);

    std::expected<RegisterResult, ServiceError> registerUser(const nlohmann::json& payload) const;
    std::expected<LoginResult, ServiceError> login(const nlohmann::json& payload) const;

  private:
    std::shared_ptr<IUserRepo> repo_;
};
