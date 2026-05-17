#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "services/auth_service.h"

namespace {

class FakeUserRepo : public IUserRepo {
  public:
    std::optional<RepoError> next_error;

    RepoResult<std::optional<models::User>> findByUsername(const std::string&) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }
        return std::nullopt;
    }

    RepoResult<std::optional<models::User>> findByEmail(const std::string&) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }
        return std::nullopt;
    }

    RepoResult<std::optional<models::User>> findById(int64_t) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }
        return std::nullopt;
    }

    RepoResult<bool> existsByUsername(const std::string&) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }
        return false;
    }

    RepoResult<bool> existsByEmail(const std::string&) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }
        return false;
    }

    RepoResult<int64_t> insert(const models::User&) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }
        return 42;
    }

  private:
    std::optional<RepoError> consumeError() {
        if (!next_error) {
            return std::nullopt;
        }

        auto error = std::move(next_error);
        next_error.reset();
        return error;
    }
};

nlohmann::json validRegisterPayload() {
    return {{"username", "repoerr"},
            {"email", "repoerr@test.com"},
            {"password", "pass123456"},
            {"nickname", "repoerr"}};
}

} // namespace

TEST(AuthServiceRepoErrors, RejectsNullRepoDependency) {
    EXPECT_THROW((AuthService(nullptr)), std::invalid_argument);
}

TEST(AuthServiceRepoErrors, RegisterMapsRepoUnavailableToServiceUnavailable) {
    auto repo = std::make_shared<FakeUserRepo>();
    repo->next_error = RepoError{RepoError::Kind::DbUnavailable, "mysql is down"};
    AuthService service(repo);

    const auto result = service.registerUser(validRegisterPayload());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k503ServiceUnavailable);
    EXPECT_EQ(result.error().code, "database_unavailable");
}

TEST(AuthServiceRepoErrors, LoginMapsRepoUnavailableToServiceUnavailable) {
    auto repo = std::make_shared<FakeUserRepo>();
    repo->next_error = RepoError{RepoError::Kind::DbUnavailable, "mysql is down"};
    AuthService service(repo);

    const auto result = service.login({{"username", "repoerr"}, {"password", "pass123456"}});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k503ServiceUnavailable);
    EXPECT_EQ(result.error().code, "database_unavailable");
}
