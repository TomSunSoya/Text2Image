#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "database/repo_error.h"
#include "models/user.h"

class IUserRepo {
  public:
    virtual ~IUserRepo() = default;

    [[nodiscard]] virtual RepoResult<std::optional<models::User>>
    findByUsername(const std::string& username) = 0;
    [[nodiscard]] virtual RepoResult<std::optional<models::User>>
    findByEmail(const std::string& email) = 0;
    [[nodiscard]] virtual RepoResult<std::optional<models::User>> findById(int64_t id) = 0;

    [[nodiscard]] virtual RepoResult<bool> existsByUsername(const std::string& username) = 0;
    [[nodiscard]] virtual RepoResult<bool> existsByEmail(const std::string& email) = 0;

    [[nodiscard]] virtual RepoResult<int64_t> insert(const models::User& user) = 0;
};
