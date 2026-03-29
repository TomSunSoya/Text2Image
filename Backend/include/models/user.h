#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace models {

class User {
  public:
    int64_t id{0};
    std::string username;
    std::string password;
    std::string email;
    std::string nickname;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    bool enabled{true};

    // exclude password
    nlohmann::json toJson() const;

    static User fromJson(const nlohmann::json& j);

    bool validate() const;
};

} // namespace models