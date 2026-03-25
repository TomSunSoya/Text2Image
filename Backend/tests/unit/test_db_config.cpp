#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "Backend.h"
#include "db_manager.h"

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
