#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

#include "services/auth_service.h"
#include "utils/password_utils.h"

namespace {

class RefreshFakeUserRepo final : public IUserRepo {
  public:
    models::User user;

    RefreshFakeUserRepo() {
        user.id = 42;
        user.username = "refreshuser";
        user.email = "refresh@test.com";
        user.nickname = "Refresh User";
        user.role = "user";
        user.password = security::hashPassword("pass123456");
        user.enabled = true;
    }

    RepoResult<std::optional<models::User>> findByUsername(const std::string& username) override {
        if (username == user.username) {
            return user;
        }
        return std::nullopt;
    }

    RepoResult<std::optional<models::User>> findByEmail(const std::string& email) override {
        if (email == user.email) {
            return user;
        }
        return std::nullopt;
    }

    RepoResult<std::optional<models::User>> findById(int64_t id) override {
        if (id == user.id) {
            return user;
        }
        return std::nullopt;
    }

    RepoResult<bool> existsByUsername(const std::string&) override {
        return false;
    }

    RepoResult<bool> existsByEmail(const std::string&) override {
        return false;
    }

    RepoResult<int64_t> insert(const models::User&) override {
        return user.id;
    }

    RepoResult<bool> updatePassword(int64_t id, const std::string& passwordHash) override {
        if (id != user.id) {
            return false;
        }
        user.password = passwordHash;
        return true;
    }
};

struct RefreshHarness {
    std::shared_ptr<RefreshFakeUserRepo> repo{std::make_shared<RefreshFakeUserRepo>()};
    std::shared_ptr<InMemoryRefreshTokenStore> store{std::make_shared<InMemoryRefreshTokenStore>()};
    AuthService service{repo, store};

    LoginResult login() {
        auto result =
            service.login({{"username", repo->user.username}, {"password", "pass123456"}});
        EXPECT_TRUE(result.has_value());
        return *result;
    }
};

} // namespace

TEST(AuthRefresh, LoginIssuesAccessAndRefreshTokens) {
    RefreshHarness harness;

    const auto login = harness.login();

    EXPECT_FALSE(login.access_token.empty());
    EXPECT_FALSE(login.refresh_token.empty());
    EXPECT_EQ(login.expires_in, 900);
}

TEST(AuthRefresh, RefreshRotatesTokenAndRejectsOldRefreshToken) {
    RefreshHarness harness;
    const auto login = harness.login();

    const auto rotated = harness.service.refresh({{"refresh_token", login.refresh_token}});

    ASSERT_TRUE(rotated.has_value());
    EXPECT_FALSE(rotated->access_token.empty());
    EXPECT_FALSE(rotated->refresh_token.empty());
    EXPECT_NE(rotated->refresh_token, login.refresh_token);

    const auto stale = harness.service.refresh({{"refresh_token", login.refresh_token}});

    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error().status, drogon::k401Unauthorized);
    EXPECT_EQ(stale.error().code, "refresh_token_revoked");
}

TEST(AuthRefresh, RotatedRefreshTokenCanBeUsedAgain) {
    RefreshHarness harness;
    const auto login = harness.login();
    const auto rotated = harness.service.refresh({{"refresh_token", login.refresh_token}});
    ASSERT_TRUE(rotated.has_value());

    const auto second = harness.service.refresh({{"refresh_token", rotated->refresh_token}});

    ASSERT_TRUE(second.has_value());
    EXPECT_NE(second->refresh_token, rotated->refresh_token);
}

TEST(AuthRefresh, LogoutRevokesRefreshToken) {
    RefreshHarness harness;
    const auto login = harness.login();

    const auto logout = harness.service.logout({{"refresh_token", login.refresh_token}});
    ASSERT_TRUE(logout.has_value());

    const auto refreshed = harness.service.refresh({{"refresh_token", login.refresh_token}});
    ASSERT_FALSE(refreshed.has_value());
    EXPECT_EQ(refreshed.error().code, "refresh_token_revoked");
}

TEST(AuthRefresh, MissingRefreshTokenIsRejected) {
    RefreshHarness harness;

    const auto result = harness.service.refresh(nlohmann::json::object());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k400BadRequest);
    EXPECT_EQ(result.error().code, "missing_refresh_token");
}

TEST(AuthProfile, GetProfileReturnsCurrentUser) {
    RefreshHarness harness;

    const auto profile = harness.service.getProfile(harness.repo->user.id);

    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->username, harness.repo->user.username);
    EXPECT_EQ(profile->email, harness.repo->user.email);
}

TEST(AuthProfile, ChangePasswordRejectsWrongOldPassword) {
    RefreshHarness harness;

    const auto result = harness.service.changePassword(
        harness.repo->user.id, {{"old_password", "wrong"}, {"new_password", "newpass123"}});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k401Unauthorized);
    EXPECT_EQ(result.error().code, "invalid_password");
}

TEST(AuthProfile, ChangePasswordRejectsShortNewPassword) {
    RefreshHarness harness;

    const auto result = harness.service.changePassword(
        harness.repo->user.id, {{"old_password", "pass123456"}, {"new_password", "123"}});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k400BadRequest);
    EXPECT_EQ(result.error().code, "invalid_password");
}

TEST(AuthProfile, ChangePasswordRevokesAllRefreshTokensAndAllowsNewPassword) {
    RefreshHarness harness;
    const auto first = harness.login();
    const auto second = harness.login();

    const auto changed = harness.service.changePassword(
        harness.repo->user.id, {{"old_password", "pass123456"}, {"new_password", "newpass123"}});

    ASSERT_TRUE(changed.has_value());

    const auto firstRefresh = harness.service.refresh({{"refresh_token", first.refresh_token}});
    EXPECT_FALSE(firstRefresh.has_value());
    EXPECT_EQ(firstRefresh.error().code, "refresh_token_revoked");

    const auto secondRefresh = harness.service.refresh({{"refresh_token", second.refresh_token}});
    EXPECT_FALSE(secondRefresh.has_value());
    EXPECT_EQ(secondRefresh.error().code, "refresh_token_revoked");

    const auto oldLogin = harness.service.login(
        {{"username", harness.repo->user.username}, {"password", "pass123456"}});
    EXPECT_FALSE(oldLogin.has_value());

    const auto newLogin = harness.service.login(
        {{"username", harness.repo->user.username}, {"password", "newpass123"}});
    EXPECT_TRUE(newLogin.has_value());
}
