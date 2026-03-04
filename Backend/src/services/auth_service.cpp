#include "auth_service.h"

#include "UserRepo.h"
#include "jwt_utils.h"

std::optional<RegisterResult> AuthService::registerUser(const nlohmann::json& payload,
                                                        ServiceError& error) const
{
    models::User user = models::User::fromJson(payload);
    if (!user.validate()) {
        error.status = drogon::k400BadRequest;
        error.message = "invalid user data";
        return std::nullopt;
    }

    UserRepo repo;
    if (repo.existsByUsername(user.username)) {
        error.status = drogon::k409Conflict;
        error.message = "username already exists";
        return std::nullopt;
    }

    if (repo.existsByEmail(user.email)) {
        error.status = drogon::k409Conflict;
        error.message = "email already exists";
        return std::nullopt;
    }

    user.id = repo.insert(user);
    return RegisterResult{user};
}

std::optional<LoginResult> AuthService::login(const nlohmann::json& payload, ServiceError& error) const
{
    const std::string username = payload.value("username", "");
    const std::string email = payload.value("email", "");
    const std::string password = payload.value("password", "");

    if (password.empty() || (username.empty() && email.empty())) {
        error.status = drogon::k400BadRequest;
        error.message = "missing credentials";
        return std::nullopt;
    }

    UserRepo repo;
    std::optional<models::User> user;

    if (!username.empty()) {
        user = repo.findByUsername(username);
    } else {
        user = repo.findByEmail(email);
    }

    if (!user || user->password != password) {
        error.status = drogon::k401Unauthorized;
        error.message = "invalid username or password";
        return std::nullopt;
    }

    return LoginResult{*user, utils::createToken(user->id, user->username)};
}
