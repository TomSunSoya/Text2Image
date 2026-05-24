#include "services/auth_service.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <utility>

#include "database/UserRepo.h"
#include "services/repo_error_mapper.h"
#include "utils/jwt_utils.h"
#include "utils/password_utils.h"

AuthService::AuthService() : AuthService(std::make_shared<UserRepo>()) {}

AuthService::AuthService(std::shared_ptr<IUserRepo> repo)
    : AuthService(std::move(repo), defaultRefreshTokenStore()) {}

AuthService::AuthService(std::shared_ptr<IUserRepo> repo,
                         std::shared_ptr<IRefreshTokenStore> tokenStore)
    : repo_(std::move(repo)), token_store_(std::move(tokenStore)) {
    if (!repo_) {
        throw std::invalid_argument("AuthService: repo must not be null");
    }
    if (!token_store_) {
        throw std::invalid_argument("AuthService: token store must not be null");
    }
}

std::expected<RegisterResult, ServiceError>
AuthService::registerUser(const nlohmann::json& payload) const {
    models::User user = models::User::fromJson(payload);
    if (!user.validate()) {
        return std::unexpected(
            ServiceError{drogon::k400BadRequest, "invalid_user_data", "invalid user data"});
    }

    auto usernameExists = repo_->existsByUsername(user.username);
    if (!usernameExists) {
        return std::unexpected(mapRepoError(usernameExists.error()));
    }
    if (*usernameExists) {
        return std::unexpected(
            ServiceError{drogon::k409Conflict, "username_exists", "username already exists"});
    }

    auto emailExists = repo_->existsByEmail(user.email);
    if (!emailExists) {
        return std::unexpected(mapRepoError(emailExists.error()));
    }
    if (*emailExists) {
        return std::unexpected(
            ServiceError{drogon::k409Conflict, "email_exists", "email already exists"});
    }

    user.role = "user";
    user.password = security::hashPassword(user.password);
    auto insertedId = repo_->insert(user);
    if (!insertedId) {
        return std::unexpected(mapRepoError(insertedId.error()));
    }

    user.id = *insertedId;
    return RegisterResult{user};
}

std::expected<LoginResult, ServiceError> AuthService::login(const nlohmann::json& payload) const {
    const std::string username = payload.value("username", "");
    const std::string email = payload.value("email", "");
    const std::string password = payload.value("password", "");

    if (password.empty() || (username.empty() && email.empty())) {
        return std::unexpected(
            ServiceError{drogon::k400BadRequest, "missing_credentials", "missing credentials"});
    }

    auto user = !username.empty() ? repo_->findByUsername(username) : repo_->findByEmail(email);
    if (!user) {
        return std::unexpected(mapRepoError(user.error()));
    }

    if (!*user || !security::verifyPassword(password, (*user)->password)) {
        return std::unexpected(ServiceError{drogon::k401Unauthorized, "invalid_credentials",
                                            "invalid username or password"});
    }

    auto issued = issueTokenPair(**user);
    if (!issued) {
        return std::unexpected(issued.error());
    }

    return LoginResult{issued->user, issued->access_token, issued->refresh_token,
                       issued->expires_in};
}

std::expected<RefreshResult, ServiceError>
AuthService::refresh(const nlohmann::json& payload) const {
    const auto refreshToken = payload.value("refresh_token", std::string{});
    if (refreshToken.empty()) {
        return std::unexpected(
            ServiceError{drogon::k400BadRequest, "missing_refresh_token", "missing refresh token"});
    }

    auto claims = utils::verifyRefreshToken(refreshToken);
    if (!claims) {
        return std::unexpected(claims.error());
    }

    auto storedUserId = token_store_->consume(claims->jti);
    if (!storedUserId) {
        return std::unexpected(storedUserId.error());
    }
    if (!*storedUserId || **storedUserId != claims->user_id) {
        return std::unexpected(ServiceError{drogon::k401Unauthorized, "refresh_token_revoked",
                                            "refresh token has been revoked"});
    }

    auto user = repo_->findById(claims->user_id);
    if (!user) {
        return std::unexpected(mapRepoError(user.error()));
    }
    if (!*user || !(*user)->enabled) {
        return std::unexpected(ServiceError{drogon::k401Unauthorized, "invalid_refresh_token",
                                            "invalid refresh token"});
    }

    return issueTokenPair(**user);
}

std::expected<void, ServiceError> AuthService::logout(const nlohmann::json& payload) const {
    const auto refreshToken = payload.value("refresh_token", std::string{});
    if (refreshToken.empty()) {
        return std::unexpected(
            ServiceError{drogon::k400BadRequest, "missing_refresh_token", "missing refresh token"});
    }

    auto claims = utils::verifyRefreshToken(refreshToken);
    if (!claims) {
        return std::unexpected(claims.error());
    }

    return token_store_->revoke(claims->jti);
}

std::expected<models::User, ServiceError> AuthService::getProfile(int64_t userId) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    auto user = repo_->findById(userId);
    if (!user) {
        return std::unexpected(mapRepoError(user.error()));
    }
    if (!*user) {
        return std::unexpected(
            ServiceError{drogon::k404NotFound, "user_not_found", "user not found"});
    }
    return **user;
}

std::expected<void, ServiceError> AuthService::changePassword(int64_t userId,
                                                              const nlohmann::json& payload) const {
    const auto oldPassword = payload.value("old_password", std::string{});
    const auto newPassword = payload.value("new_password", std::string{});

    if (oldPassword.empty() || newPassword.empty()) {
        return std::unexpected(ServiceError{drogon::k400BadRequest, "missing_password",
                                            "old_password and new_password are required"});
    }
    if (newPassword.size() < 6) {
        return std::unexpected(ServiceError{drogon::k400BadRequest, "invalid_password",
                                            "new password must be at least 6 characters"});
    }

    auto user = getProfile(userId);
    if (!user) {
        return std::unexpected(user.error());
    }
    if (!security::verifyPassword(oldPassword, user->password)) {
        return std::unexpected(ServiceError{drogon::k401Unauthorized, "invalid_password",
                                            "old password is incorrect"});
    }

    auto revoked = token_store_->revokeUser(userId);
    if (!revoked) {
        return std::unexpected(revoked.error());
    }

    auto updated = repo_->updatePassword(userId, security::hashPassword(newPassword));
    if (!updated) {
        return std::unexpected(mapRepoError(updated.error()));
    }
    if (!*updated) {
        return std::unexpected(
            ServiceError{drogon::k404NotFound, "user_not_found", "user not found"});
    }
    return {};
}

std::expected<RefreshResult, ServiceError>
AuthService::issueTokenPair(const models::User& user) const {
    const auto refreshJti = utils::generateJti();
    auto stored = token_store_->store(refreshJti, user.id,
                                      std::chrono::seconds(utils::refreshTokenExpiresInSeconds()));
    if (!stored) {
        return std::unexpected(stored.error());
    }

    return RefreshResult{user, utils::createToken(user.id, user.username, user.role),
                         utils::issueRefreshToken(user.id, refreshJti),
                         utils::accessTokenExpiresInSeconds()};
}
