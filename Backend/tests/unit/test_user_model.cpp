#include <gtest/gtest.h>
#include "user.h"

// ==================== fromJson ====================

TEST(User_FromJson, AllFields) {
    nlohmann::json j = {{"id", 1},
                        {"username", "alice"},
                        {"password", "secret123"},
                        {"email", "alice@test.com"},
                        {"nickname", "Alice"},
                        {"enabled", true}};

    auto user = models::User::fromJson(j);
    EXPECT_EQ(user.id, 1);
    EXPECT_EQ(user.username, "alice");
    EXPECT_EQ(user.password, "secret123");
    EXPECT_EQ(user.email, "alice@test.com");
    EXPECT_EQ(user.nickname, "Alice");
    EXPECT_TRUE(user.enabled);
}

TEST(User_FromJson, MissingFields) {
    auto user = models::User::fromJson(nlohmann::json::object());
    EXPECT_EQ(user.id, 0);
    EXPECT_EQ(user.username, "");
    EXPECT_EQ(user.password, "");
    EXPECT_EQ(user.email, "");
}

// ==================== toJson ====================

TEST(User_ToJson, ExcludesPassword) {
    models::User user;
    user.id = 42;
    user.username = "bob";
    user.password = "should-not-appear";
    user.email = "bob@test.com";
    user.nickname = "Bob";

    auto j = user.toJson();
    EXPECT_EQ(j.at("id"), 42);
    EXPECT_EQ(j.at("username"), "bob");
    EXPECT_EQ(j.at("email"), "bob@test.com");
    EXPECT_EQ(j.at("nickname"), "Bob");
    EXPECT_FALSE(j.contains("password")); // password must be excluded
}

// ==================== validate ====================

TEST(User_Validate, ValidUser) {
    models::User user;
    user.username = "alice";   // 5 chars, within [3, 20]
    user.password = "pass123"; // 7 chars, >= 6
    user.email = "alice@test.com";
    EXPECT_TRUE(user.validate());
}

TEST(User_Validate, UsernameTooShort) {
    models::User user;
    user.username = "ab"; // 2 chars, < 3
    user.password = "pass123";
    user.email = "a@b.com";
    EXPECT_FALSE(user.validate());
}

TEST(User_Validate, UsernameTooLong) {
    models::User user;
    user.username = std::string(21, 'a'); // 21 chars, > 20
    user.password = "pass123";
    user.email = "a@b.com";
    EXPECT_FALSE(user.validate());
}

TEST(User_Validate, UsernameExactBoundaries) {
    models::User user;
    user.password = "pass123";
    user.email = "a@b.com";

    user.username = "abc"; // exactly 3
    EXPECT_TRUE(user.validate());

    user.username = std::string(20, 'a'); // exactly 20
    EXPECT_TRUE(user.validate());
}

TEST(User_Validate, PasswordTooShort) {
    models::User user;
    user.username = "alice";
    user.password = "12345"; // 5 chars, < 6
    user.email = "a@b.com";
    EXPECT_FALSE(user.validate());
}

TEST(User_Validate, PasswordExactMinimum) {
    models::User user;
    user.username = "alice";
    user.password = "123456"; // exactly 6
    user.email = "a@b.com";
    EXPECT_TRUE(user.validate());
}

TEST(User_Validate, InvalidEmails) {
    models::User user;
    user.username = "alice";
    user.password = "pass123";

    user.email = "";
    EXPECT_FALSE(user.validate());

    user.email = "not-an-email";
    EXPECT_FALSE(user.validate());

    user.email = "@missing.com";
    EXPECT_FALSE(user.validate());

    user.email = "missing@";
    EXPECT_FALSE(user.validate());

    user.email = "a@b"; // no TLD
    EXPECT_FALSE(user.validate());
}

TEST(User_Validate, ValidEmails) {
    models::User user;
    user.username = "alice";
    user.password = "pass123";

    user.email = "user@example.com";
    EXPECT_TRUE(user.validate());

    user.email = "user.name+tag@domain.co.uk";
    EXPECT_TRUE(user.validate());

    user.email = "test123@test.io";
    EXPECT_TRUE(user.validate());
}
