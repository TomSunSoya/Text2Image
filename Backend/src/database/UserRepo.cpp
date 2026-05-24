#include "database/UserRepo.h"

#include "database/db_manager.h"
#include "database/repo_invoke.h"

#include <stdexcept>
#include <utility>

#include <mysqlx/xdevapi.h>

namespace {

models::User rowToUser(const mysqlx::Row& row) {
    try {
        models::User user;
        user.id = static_cast<int64_t>(row[0].get<int64_t>());
        user.username = row[1].get<std::string>();
        user.email = row[2].get<std::string>();
        user.password = row[3].get<std::string>();
        user.nickname = row[4].get<std::string>();
        user.role = row[5].get<std::string>();
        return user;
    } catch (const std::exception& ex) {
        throw RepoSerializationError(ex.what());
    }
}

mysqlx::Table usersTable() {
    return database::DBManager::threadSchema().getTable("users", true);
}

} // namespace

RepoResult<std::optional<models::User>> UserRepo::findByUsername(const std::string& username) {
    return repoInvoke([&] -> std::optional<models::User> {
        auto res = usersTable()
                       .select("id", "username", "email", "password", "nickname", "role")
                       .where("username = :u")
                       .bind("u", username)
                       .execute();

        auto row = res.fetchOne();
        if (!row) {
            return std::nullopt;
        }
        return rowToUser(row);
    });
}

RepoResult<std::optional<models::User>> UserRepo::findByEmail(const std::string& email) {
    return repoInvoke([&] -> std::optional<models::User> {
        auto res = usersTable()
                       .select("id", "username", "email", "password", "nickname", "role")
                       .where("email = :e")
                       .bind("e", email)
                       .execute();

        auto row = res.fetchOne();
        if (!row) {
            return std::nullopt;
        }
        return rowToUser(row);
    });
}

RepoResult<std::optional<models::User>> UserRepo::findById(int64_t id) {
    return repoInvoke([&] -> std::optional<models::User> {
        auto res = usersTable()
                       .select("id", "username", "email", "password", "nickname", "role")
                       .where("id = :id")
                       .bind("id", id)
                       .execute();

        auto row = res.fetchOne();
        if (!row) {
            return std::nullopt;
        }
        return rowToUser(row);
    });
}

RepoResult<bool> UserRepo::existsByUsername(const std::string& username) {
    return repoInvoke([&] {
        auto res = usersTable()
                       .select("id", "username", "email", "password", "nickname", "role")
                       .where("username = :u")
                       .bind("u", username)
                       .execute();

        return static_cast<bool>(res.fetchOne());
    });
}

RepoResult<bool> UserRepo::existsByEmail(const std::string& email) {
    return repoInvoke([&] {
        auto res = usersTable()
                       .select("id", "username", "email", "password", "nickname", "role")
                       .where("email = :e")
                       .bind("e", email)
                       .execute();

        return static_cast<bool>(res.fetchOne());
    });
}

RepoResult<int64_t> UserRepo::insert(const models::User& user) {
    return repoInvoke([&] {
        auto res = usersTable()
                       .insert("username", "email", "password", "nickname", "role")
                       .values(user.username, user.email, user.password, user.nickname, user.role)
                       .execute();
        return static_cast<int64_t>(res.getAutoIncrementValue());
    });
}

RepoResult<bool> UserRepo::updatePassword(int64_t id, const std::string& passwordHash) {
    return repoInvoke([&] {
        auto result = usersTable()
                          .update()
                          .set("password", passwordHash)
                          .where("id = :id")
                          .bind("id", id)
                          .execute();
        return result.getAffectedItemsCount() > 0;
    });
}
