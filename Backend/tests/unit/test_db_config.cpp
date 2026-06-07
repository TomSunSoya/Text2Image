#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "Backend.h"
#include "database/db_manager.h"

namespace {

std::optional<std::string> readEnvVar(const char* name) {
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
    return value;
}

class ScopedEnvVar {
  public:
    ScopedEnvVar(const char* name, std::optional<std::string> value)
        : name_(name), original_(readEnvVar(name)) {
        set(value);
    }

    ~ScopedEnvVar() {
        set(original_);
    }

  private:
    void set(const std::optional<std::string>& value) {
#ifdef _WIN32
        if (value.has_value()) {
            _putenv_s(name_.c_str(), value->c_str());
        } else {
            _putenv_s(name_.c_str(), "");
        }
#else
        if (value.has_value()) {
            setenv(name_.c_str(), value->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
#endif
    }

    std::string name_;
    std::optional<std::string> original_;
};

std::filesystem::path writeTempConfig(const nlohmann::json& config) {
    const auto fileName =
        "backend-config-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
    const auto path = std::filesystem::temp_directory_path() / fileName;

    std::ofstream out(path);
    out << config.dump(2);
    out.close();

    return path;
}

std::filesystem::path writeTempTextFile(const std::string& prefix, const std::string& content) {
    const auto fileName =
        prefix + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / fileName;

    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();

    return path;
}

} // namespace

TEST(MysqlConfig, ParseLeavesSslUnsetWhenMissing) {
    const nlohmann::json dbConfig = {
        {"host", "127.0.0.1"},
        {"port", 33060},
        {"username", "user"},
        {"password", "secret"},
        {"database", "image_generator"},
    };

    const auto cfg = database::parseMysqlConfig(dbConfig);

    EXPECT_EQ(cfg.host, "127.0.0.1");
    EXPECT_EQ(cfg.port, 33060);
    EXPECT_FALSE(cfg.ssl.has_value());
    EXPECT_EQ(cfg.pool_size, 10);
}

TEST(MysqlConfig, ParseReadsExplicitSslFlag) {
    nlohmann::json dbConfig = {
        {"host", "127.0.0.1"},
        {"port", 33060},
        {"username", "user"},
        {"password", "secret"},
        {"database", "image_generator"},
        {"ssl", false},
    };

    auto cfg = database::parseMysqlConfig(dbConfig);
    ASSERT_TRUE(cfg.ssl.has_value());
    EXPECT_FALSE(*cfg.ssl);

    dbConfig["ssl"] = true;
    cfg = database::parseMysqlConfig(dbConfig);
    ASSERT_TRUE(cfg.ssl.has_value());
    EXPECT_TRUE(*cfg.ssl);
}

TEST(MysqlConfig, ParseReadsAndClampsPoolSize) {
    nlohmann::json dbConfig = {
        {"host", "127.0.0.1"},
        {"port", 33060},
        {"username", "user"},
        {"password", "secret"},
        {"database", "image_generator"},
        {"pool_size", 24},
    };

    auto cfg = database::parseMysqlConfig(dbConfig);
    EXPECT_EQ(cfg.pool_size, 24);

    dbConfig["pool_size"] = 0;
    cfg = database::parseMysqlConfig(dbConfig);
    EXPECT_EQ(cfg.pool_size, 1);
}

TEST(BackendConfig, LoadConfigAppliesDatabasePoolSizeEnvOverride) {
    const ScopedEnvVar poolOverride("DB_POOL_SIZE", std::string("32"));
    const auto path = writeTempConfig({{"database", {{"host", "db.internal"}, {"pool_size", 10}}}});

    const auto config = backend::loadConfig(path.string());

    EXPECT_EQ(config.at("database").at("pool_size").get<int>(), 32);

    std::filesystem::remove(path);
}

TEST(BackendConfig, LoadConfigAppliesDatabaseSslEnvOverride) {
    const ScopedEnvVar sslOverride("DB_SSL", std::string("false"));
    const auto path = writeTempConfig({{"database", {{"host", "db.internal"}}}});

    const auto config = backend::loadConfig(path.string());

    ASSERT_TRUE(config.at("database").contains("ssl"));
    ASSERT_TRUE(config.at("database").at("ssl").is_boolean());
    EXPECT_FALSE(config.at("database").at("ssl").get<bool>());

    std::filesystem::remove(path);
}

TEST(BackendConfig, LoadConfigIgnoresInvalidDatabaseSslEnvOverride) {
    const ScopedEnvVar sslOverride("DB_SSL", std::string("not-a-bool"));
    const auto path = writeTempConfig({{"database", {{"host", "db.internal"}}}});

    const auto config = backend::loadConfig(path.string());

    EXPECT_FALSE(config.at("database").contains("ssl"));

    std::filesystem::remove(path);
}

TEST(BackendConfig, LoadConfigReadsSecretFileEnvOverrides) {
    const auto dbSecret = writeTempTextFile("db-secret", "db-from-file\n");
    const auto jwtSecret = writeTempTextFile("jwt-secret", "jwt-from-file\r\n");
    const auto redisSecret = writeTempTextFile("redis-secret", "redis-from-file\n");
    const auto cacheSecret = writeTempTextFile("cache-secret", "cache-from-file\n");
    const auto minioSecret = writeTempTextFile("minio-secret", "minio-from-file\n");

    const ScopedEnvVar dbFile("DB_PASSWORD_FILE", dbSecret.string());
    const ScopedEnvVar jwtFile("JWT_SECRET_FILE", jwtSecret.string());
    const ScopedEnvVar redisFile("REDIS_PASSWORD_FILE", redisSecret.string());
    const ScopedEnvVar cacheFile("CACHE_PASSWORD_FILE", cacheSecret.string());
    const ScopedEnvVar minioFile("MINIO_SECRET_KEY_FILE", minioSecret.string());

    const ScopedEnvVar dbPlain("DB_PASSWORD", std::string("db-from-env"));
    const ScopedEnvVar jwtPlain("JWT_SECRET", std::string("jwt-from-env"));
    const ScopedEnvVar redisPlain("REDIS_PASSWORD", std::string("redis-from-env"));
    const ScopedEnvVar cachePlain("CACHE_PASSWORD", std::string("cache-from-env"));
    const ScopedEnvVar minioPlain("MINIO_SECRET_KEY", std::string("minio-from-env"));

    const auto path = writeTempConfig({
        {"database", {{"password", "db-from-config"}}},
        {"jwt", {{"secret", "jwt-from-config"}}},
        {"redis", {{"password", "redis-from-config"}}},
        {"cache", {{"password", "cache-from-config"}}},
        {"minio", {{"secret_key", "minio-from-config"}}},
    });

    const auto config = backend::loadConfig(path.string());

    EXPECT_EQ(config.at("database").at("password").get<std::string>(), "db-from-file");
    EXPECT_EQ(config.at("jwt").at("secret").get<std::string>(), "jwt-from-file");
    EXPECT_EQ(config.at("redis").at("password").get<std::string>(), "redis-from-file");
    EXPECT_EQ(config.at("cache").at("password").get<std::string>(), "cache-from-file");
    EXPECT_EQ(config.at("minio").at("secret_key").get<std::string>(), "minio-from-file");

    std::filesystem::remove(path);
    std::filesystem::remove(dbSecret);
    std::filesystem::remove(jwtSecret);
    std::filesystem::remove(redisSecret);
    std::filesystem::remove(cacheSecret);
    std::filesystem::remove(minioSecret);
}

TEST(BackendConfig, LoadConfigFallsBackToPlainSecretEnvWhenFileEnvMissing) {
    const ScopedEnvVar jwtFile("JWT_SECRET_FILE", std::nullopt);
    const ScopedEnvVar jwtPlain("JWT_SECRET", std::string("jwt-from-env"));
    const auto path = writeTempConfig({{"jwt", {{"secret", "jwt-from-config"}}}});

    const auto config = backend::loadConfig(path.string());

    EXPECT_EQ(config.at("jwt").at("secret").get<std::string>(), "jwt-from-env");

    std::filesystem::remove(path);
}

TEST(BackendConfig, LoadConfigReportsMissingSecretFileClearly) {
    const auto missingPath =
        std::filesystem::temp_directory_path() /
        ("missing-jwt-secret-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const ScopedEnvVar jwtFile("JWT_SECRET_FILE", missingPath.string());
    const auto path = writeTempConfig({{"jwt", {{"secret", "jwt-from-config"}}}});

    try {
        (void)backend::loadConfig(path.string());
        FAIL() << "loadConfig should reject missing JWT_SECRET_FILE";
    } catch (const std::runtime_error& ex) {
        const std::string message = ex.what();
        EXPECT_NE(message.find("JWT_SECRET_FILE points to unreadable secret file"),
                  std::string::npos);
        EXPECT_NE(message.find(missingPath.string()), std::string::npos);
    }

    std::filesystem::remove(path);
}

TEST(BackendConfig, LoadConfigReportsEmptySecretFileClearly) {
    const auto emptySecret = writeTempTextFile("empty-jwt-secret", "");
    const ScopedEnvVar jwtFile("JWT_SECRET_FILE", emptySecret.string());
    const auto path = writeTempConfig({{"jwt", {{"secret", "jwt-from-config"}}}});

    try {
        (void)backend::loadConfig(path.string());
        FAIL() << "loadConfig should reject empty JWT_SECRET_FILE";
    } catch (const std::runtime_error& ex) {
        const std::string message = ex.what();
        EXPECT_NE(message.find("JWT_SECRET_FILE points to empty secret file"), std::string::npos);
        EXPECT_NE(message.find(emptySecret.string()), std::string::npos);
    }

    std::filesystem::remove(path);
    std::filesystem::remove(emptySecret);
}
