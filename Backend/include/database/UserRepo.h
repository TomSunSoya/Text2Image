#pragma once

#include "models/user.h"
#include <optional>
#include <string>

class UserRepo {
  public:
    std::optional<models::User> findByUsername(const std::string& username);
    std::optional<models::User> findByEmail(const std::string& email);
    std::optional<models::User> findById(int64_t id);

    bool existsByUsername(const std::string& username);
    bool existsByEmail(const std::string& email);

    int64_t insert(const models::User& user);
};
