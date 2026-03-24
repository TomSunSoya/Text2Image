#include <gtest/gtest.h>

#include "auth_service.h"
#include "jwt_utils.h"
#include "test_db_support.h"

class AuthFlowTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_support::ensureTestDatabase();
        test_support::cleanUsers();
    }

    void TearDown() override {
        test_support::cleanUsers();
    }

    static nlohmann::json makeRegPayload(const std::string& username, const std::string& email,
                                         const std::string& password = "pass123456") {
        return {{"username", username},
                {"email", email},
                {"password", password},
                {"nickname", username}};
    }
};

// ==================== register ====================

TEST_F(AuthFlowTest, RegisterSuccess) {
    AuthService service;
    auto result = service.registerUser(makeRegPayload("alice", "alice@test.com"));
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->user.id, 0);
    EXPECT_EQ(result->user.username, "alice");
}

TEST_F(AuthFlowTest, RegisterDuplicateUsername) {
    AuthService service;
    ASSERT_TRUE(service.registerUser(makeRegPayload("bob", "bob@test.com")).has_value());

    auto dup = service.registerUser(makeRegPayload("bob", "bob2@test.com"));
    ASSERT_FALSE(dup.has_value());
    EXPECT_EQ(dup.error().code, "username_exists");
}

TEST_F(AuthFlowTest, RegisterDuplicateEmail) {
    AuthService service;
    ASSERT_TRUE(service.registerUser(makeRegPayload("carol", "same@test.com")).has_value());

    auto dup = service.registerUser(makeRegPayload("carol2", "same@test.com"));
    ASSERT_FALSE(dup.has_value());
    EXPECT_EQ(dup.error().code, "email_exists");
}

TEST_F(AuthFlowTest, RegisterInvalidDataRejected) {
    AuthService service;

    // username too short
    auto result = service.registerUser(makeRegPayload("ab", "ok@test.com"));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "invalid_user_data");
}

// ==================== login ====================

TEST_F(AuthFlowTest, LoginByUsernameSuccess) {
    AuthService service;
    ASSERT_TRUE(
        service.registerUser(makeRegPayload("dave", "dave@test.com", "mypass123")).has_value());

    auto login = service.login({{"username", "dave"}, {"password", "mypass123"}});
    ASSERT_TRUE(login.has_value());
    EXPECT_FALSE(login->token.empty());
    EXPECT_EQ(login->user.username, "dave");
}

TEST_F(AuthFlowTest, LoginByEmailSuccess) {
    AuthService service;
    ASSERT_TRUE(
        service.registerUser(makeRegPayload("eve", "eve@test.com", "mypass123")).has_value());

    auto login = service.login({{"email", "eve@test.com"}, {"password", "mypass123"}});
    ASSERT_TRUE(login.has_value());
    EXPECT_EQ(login->user.email, "eve@test.com");
}

TEST_F(AuthFlowTest, LoginWrongPassword) {
    AuthService service;
    ASSERT_TRUE(
        service.registerUser(makeRegPayload("frank", "frank@test.com", "right123")).has_value());

    auto login = service.login({{"username", "frank"}, {"password", "wrong"}});
    ASSERT_FALSE(login.has_value());
    EXPECT_EQ(login.error().code, "invalid_credentials");
}

TEST_F(AuthFlowTest, LoginNonexistentUser) {
    AuthService service;
    auto login = service.login({{"username", "nobody"}, {"password", "pass"}});
    ASSERT_FALSE(login.has_value());
    EXPECT_EQ(login.error().code, "invalid_credentials");
}

TEST_F(AuthFlowTest, LoginMissingCredentials) {
    AuthService service;
    auto login = service.login({{"username", ""}, {"password", ""}});
    ASSERT_FALSE(login.has_value());
    EXPECT_EQ(login.error().code, "missing_credentials");
}

// ==================== login token is valid JWT ====================

TEST_F(AuthFlowTest, LoginTokenCanBeVerified) {
    AuthService service;
    ASSERT_TRUE(service.registerUser(makeRegPayload("grace", "grace@test.com")).has_value());

    auto login = service.login({{"username", "grace"}, {"password", "pass123456"}});
    ASSERT_TRUE(login.has_value());

    auto payload = utils::verifyToken(login->token);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->username, "grace");
    EXPECT_GT(payload->user_id, 0);
}
