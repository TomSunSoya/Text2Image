#include "auth_service.h"

#include "UserRepo.h"
#include "jwt_utils.h"
#include "password_utils.h"

std::expected<RegisterResult, ServiceError> AuthService::registerUser(const nlohmann::json& payload) const
{
    models::User user = models::User::fromJson(payload);
    if (!user.validate()) {
        return std::unexpected(ServiceError{drogon::k400BadRequest, "invalid_user_data", "invalid user data"});
    }

    UserRepo repo;
    if (repo.existsByUsername(user.username)) {
        return std::unexpected(ServiceError{drogon::k409Conflict, "username_exists", "username already exists"});
    }

    if (repo.existsByEmail(user.email)) {
        return std::unexpected(ServiceError{drogon::k409Conflict, "email_exists", "email already exists"});
    }

	user.password = security::hashPassword(user.password);
    user.id = repo.insert(user);
    return RegisterResult{user};
}

std::expected<LoginResult, ServiceError> AuthService::login(const nlohmann::json& payload) const
{
    const std::string username = payload.value("username", "");
    const std::string email = payload.value("email", "");
    const std::string password = payload.value("password", "");

    if (password.empty() || (username.empty() && email.empty())) {
        return std::unexpected(ServiceError{drogon::k400BadRequest, "missing_credentials", "missing credentials"});
    }

    UserRepo repo;
    std::optional<models::User> user;

    if (!username.empty()) {
        user = repo.findByUsername(username);
    } else {
        user = repo.findByEmail(email);
    }

    if (!user || !security::verifyPassword(password, user->password)) {
        return std::unexpected(ServiceError{drogon::k401Unauthorized, "invalid_credentials", "invalid username or password"});
    }

    return LoginResult{*user, utils::createToken(user->id, user->username)};
}
