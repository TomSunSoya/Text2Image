#include "services/auth_service.h"

#include "database/UserRepo.h"
#include "services/repo_error_mapper.h"
#include "utils/jwt_utils.h"
#include "utils/password_utils.h"

#include <memory>
#include <stdexcept>
#include <utility>

AuthService::AuthService() : AuthService(std::make_shared<UserRepo>()) {}

AuthService::AuthService(std::shared_ptr<IUserRepo> repo) : repo_(std::move(repo)) {
    if (!repo_) {
        throw std::invalid_argument("AuthService: repo must not be null");
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

    return LoginResult{**user, utils::createToken((*user)->id, (*user)->username, (*user)->role)};
}
