#include "test_db_support.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include <mysqlx/xdevapi.h>

#include "Backend.h"
#include "ImageRepo.h"

namespace test_support {

namespace {

std::optional<std::string> readEnv(const char* name)
{
#ifdef _WIN32
    char* raw = nullptr;
    size_t size = 0;
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
        return std::nullopt;
    }

    std::string value(raw);
    free(raw);
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return std::nullopt;
    }

    std::string value(raw);
#endif
    if (value.empty()) {
        return std::nullopt;
    }

    return value;
}

std::string trim(std::string value)
{
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

int readEnvInt(const char* name, int fallback)
{
    if (const auto value = readEnv(name)) {
        try {
            return std::stoi(*value);
        } catch (...) {
        }
    }

    return fallback;
}

std::string resolveTestDatabaseName(const std::string& configuredName)
{
    if (const auto explicitName = readEnv("TEST_DB_NAME")) {
        return *explicitName;
    }

    if (configuredName.empty()) {
        return "image_generator_test";
    }

    if (configuredName.ends_with("_test")) {
        return configuredName;
    }

    return configuredName + "_test";
}

std::string escapeIdentifier(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '`') {
            escaped += "``";
        } else {
            escaped += ch;
        }
    }
    return escaped;
}

std::string qualifiedName(const std::string& schema, const std::string& table)
{
    return "`" + escapeIdentifier(schema) + "`.`" + escapeIdentifier(table) + "`";
}

void setWorkersDisabledForIntegrationTests()
{
    if (readEnv("TASK_ENGINE_WORKERS").has_value()) {
        return;
    }

#ifdef _WIN32
    _putenv_s("TASK_ENGINE_WORKERS", "0");
#else
    setenv("TASK_ENGINE_WORKERS", "0", 0);
#endif
}

void ensureUsersTable()
{
    const auto cfg = testDbConfig();
    database::DBManager::threadSession().sql(
        "CREATE TABLE IF NOT EXISTS " + qualifiedName(cfg.database, "users") + R"(
        (
            id BIGINT AUTO_INCREMENT PRIMARY KEY,
            username VARCHAR(64) NOT NULL,
            email VARCHAR(255) NOT NULL,
            password VARCHAR(255) NOT NULL,
            nickname VARCHAR(128) NOT NULL DEFAULT '',
            enabled BOOLEAN NOT NULL DEFAULT TRUE,
            created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
            UNIQUE KEY uq_users_username (username),
            UNIQUE KEY uq_users_email (email)
        )
    )")
        .execute();
}

void ensureImageTable()
{
    ImageRepo repo;
    (void)repo.findByUserId(0, 0, 1);
}

} // namespace

database::MysqlConfig testDbConfig()
{
    const auto config = backend::loadConfig();
    const auto& dbConfig = config.at("database");

    database::MysqlConfig cfg;
    cfg.host = trim(readEnv("TEST_DB_HOST").value_or(dbConfig.value("host", std::string("127.0.0.1"))));
    cfg.port = readEnvInt("TEST_DB_PORT", dbConfig.value("port", 33060));
    cfg.user = readEnv("TEST_DB_USERNAME").value_or(dbConfig.value("username", std::string{}));
    cfg.password = readEnv("TEST_DB_PASSWORD").value_or(dbConfig.value("password", std::string{}));
    cfg.database = resolveTestDatabaseName(dbConfig.value("database", std::string{}));
    return cfg;
}

std::string qualifiedTableName(const std::string& tableName)
{
    return qualifiedName(testDbConfig().database, tableName);
}

void ensureTestDatabase()
{
    static std::once_flag once;
    std::call_once(once, [] {
        setWorkersDisabledForIntegrationTests();

        const auto cfg = testDbConfig();
        mysqlx::Session admin(mysqlx::SessionSettings(cfg.host, cfg.port, cfg.user, cfg.password));
        admin.sql("CREATE DATABASE IF NOT EXISTS `" + escapeIdentifier(cfg.database) + "`").execute();

        database::DBManager::init(cfg);
        ensureUsersTable();
        ensureImageTable();
    });
}

void cleanUsers()
{
    ensureTestDatabase();
    database::DBManager::threadSession()
        .sql("DELETE FROM " + qualifiedTableName("users"))
        .execute();
}

void cleanTables()
{
    ensureTestDatabase();
    database::DBManager::threadSession()
        .sql("DELETE FROM " + qualifiedTableName("image_generations"))
        .execute();
    database::DBManager::threadSession()
        .sql("DELETE FROM " + qualifiedTableName("users"))
        .execute();
}

} // namespace test_support
