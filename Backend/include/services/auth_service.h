#pragma once

#include <expected>
#include <memory>
#include <string>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

#include "database/i_user_repo.h"
#include "models/user.h"
#include "services/refresh_token_store.h"
#include "services/service_error.h"

struct RegisterResult {
    models::User user;
};

struct LoginResult {
    models::User user;
    std::string access_token;
    std::string refresh_token;
    int expires_in{0};
};

struct RefreshResult {
    models::User user;
    std::string access_token;
    std::string refresh_token;
    int expires_in{0};
};

class AuthService {
  public:
    AuthService();
    explicit AuthService(std::shared_ptr<IUserRepo> repo);
    AuthService(std::shared_ptr<IUserRepo> repo, std::shared_ptr<IRefreshTokenStore> tokenStore);

    std::expected<RegisterResult, ServiceError> registerUser(const nlohmann::json& payload) const;
    std::expected<LoginResult, ServiceError> login(const nlohmann::json& payload) const;
    std::expected<RefreshResult, ServiceError> refresh(const nlohmann::json& payload) const;
    std::expected<void, ServiceError> logout(const nlohmann::json& payload) const;
    std::expected<models::User, ServiceError> getProfile(int64_t userId) const;
    std::expected<void, ServiceError> changePassword(int64_t userId,
                                                     const nlohmann::json& payload) const;

  private:
    std::expected<RefreshResult, ServiceError> issueTokenPair(const models::User& user) const;

    std::shared_ptr<IUserRepo> repo_;
    std::shared_ptr<IRefreshTokenStore> token_store_;
};
