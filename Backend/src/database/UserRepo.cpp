#include "UserRepo.h"
#include "db_manager.h"
#include <mysqlx/xdevapi.h>

static models::User rowToUser(const mysqlx::Row& row) {
	models::User user;
	user.id = static_cast<int64_t>(row[0].get<int64_t>());
	user.username = row[1].get<std::string>();
	user.email = row[2].get<std::string>();
	user.password = row[3].get<std::string>();
	user.nickname = row[4].get<std::string>();
	return user;
}

static mysqlx::Table usersTable() {
	return database::DBManager::threadSchema().getTable("users", true);
}

std::optional<models::User> UserRepo::findByUsername(const std::string& username)
{
	auto res = usersTable()
		.select("id", "username", "email", "password", "nickname")
		.where("username = :u")
		.bind("u", username)
		.execute();

	auto row = res.fetchOne();
	if (!row) return std::nullopt;
	return rowToUser(row);
}

std::optional<models::User> UserRepo::findByEmail(const std::string& email)
{
	auto res = usersTable()
		.select("id", "username", "email", "password", "nickname")
		.where("email = :e")
		.bind("e", email)
		.execute();

	auto row = res.fetchOne();
	if (!row) return std::nullopt;
	return rowToUser(row);
}

std::optional<models::User> UserRepo::findById(int64_t id)
{
	auto res = usersTable()
		.select("id", "username", "email", "password", "nickname")
		.where("id = :id")
		.bind("id", id)
		.execute();

	auto row = res.fetchOne();
	if (!row) return std::nullopt;
	return rowToUser(row);
}

bool UserRepo::existsByUsername(const std::string& username)
{
	auto res = usersTable()
		.select("id", "username", "email", "password", "nickname")
		.where("username = :u")
		.bind("u", username)
		.execute();

	return res.fetchOne();
}

bool UserRepo::existsByEmail(const std::string& email)
{
	auto res = usersTable()
		.select("id", "username", "email", "password", "nickname")
		.where("email = :e")
		.bind("e", email)
		.execute();

	return res.fetchOne();
}

int64_t UserRepo::insert(const models::User& user)
{
	auto res = usersTable()
		.insert("username", "email", "password", "nickname")
		.values(user.username, user.email, user.password, user.nickname)
		.execute();
	return static_cast<int64_t>(res.getAutoIncrementValue());
}
