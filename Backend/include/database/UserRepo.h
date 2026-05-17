#pragma once

#include <cstdint>
#include <string>

#include "database/i_user_repo.h"

class UserRepo : public IUserRepo {
  public:
    [[nodiscard]] RepoResult<std::optional<models::User>>
    findByUsername(const std::string& username) override;
    [[nodiscard]] RepoResult<std::optional<models::User>>
    findByEmail(const std::string& email) override;
    [[nodiscard]] RepoResult<std::optional<models::User>> findById(int64_t id) override;

    [[nodiscard]] RepoResult<bool> existsByUsername(const std::string& username) override;
    [[nodiscard]] RepoResult<bool> existsByEmail(const std::string& email) override;

    [[nodiscard]] RepoResult<int64_t> insert(const models::User& user) override;
};
