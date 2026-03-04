#include "user.h"
#include <regex>

nlohmann::json models::User::toJson() const
{
    return {
        {"id", id},
        {"username", username},
        {"email", email},
        {"nickname", nickname},
        {"enabled", enabled}
    };
}

models::User models::User::fromJson(const nlohmann::json& j)
{
    User user;
    if (j.contains("id")) user.id = j["id"].get<int64_t>();
    if (j.contains("username")) user.username = j["username"].get<std::string>();
    if (j.contains("password")) user.password = j["password"].get<std::string>();
    if (j.contains("email")) user.email = j["email"].get<std::string>();
    if (j.contains("nickname")) user.nickname = j["nickname"].get<std::string>();
    if (j.contains("enabled")) user.enabled = j["enabled"].get<bool>();
    return user;
}

bool models::User::validate() const
{
    if (username.length() < 3 || username.length() > 20)
        return false;

    if (password.length() < 6)
        return false;

    std::regex email_pattern(R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
    if (!std::regex_match(email, email_pattern))
        return false;
    return true;
}
