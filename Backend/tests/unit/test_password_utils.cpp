#include <gtest/gtest.h>
#include "utils/password_utils.h"

TEST(Password, HashAndVerifyRoundTrip) {
    const auto hashed = security::hashPassword("my-secret-123");
    EXPECT_TRUE(security::verifyPassword("my-secret-123", hashed));
}

TEST(Password, WrongPasswordFails) {
    const auto hashed = security::hashPassword("correct");
    EXPECT_FALSE(security::verifyPassword("wrong", hashed));
    EXPECT_FALSE(security::verifyPassword("Correct", hashed));  // case-sensitive
    EXPECT_FALSE(security::verifyPassword("correct ", hashed)); // trailing space
}

TEST(Password, DifferentSaltsProduceDifferentHashes) {
    const auto h1 = security::hashPassword("same");
    const auto h2 = security::hashPassword("same");
    EXPECT_NE(h1, h2); // different salt each time
    EXPECT_TRUE(security::verifyPassword("same", h1));
    EXPECT_TRUE(security::verifyPassword("same", h2));
}

TEST(Password, HashFormat) {
    const auto hashed = security::hashPassword("test");
    // format: pbkdf2_sha256$100000${salt_hex_32chars}${hash_hex_64chars}
    EXPECT_TRUE(hashed.starts_with("pbkdf2_sha256$100000$"));

    int dollarCount = 0;
    for (char c : hashed) {
        if (c == '$')
            ++dollarCount;
    }
    EXPECT_EQ(dollarCount, 3);
}

TEST(Password, MalformedStoredHashReturnsFalse) {
    EXPECT_FALSE(security::verifyPassword("anything", ""));
    EXPECT_FALSE(security::verifyPassword("anything", "not-a-hash"));
    EXPECT_FALSE(security::verifyPassword("anything", "a$b$c$d"));
    EXPECT_FALSE(security::verifyPassword("anything", "wrong_algo$100000$aabb$ccdd"));
}

TEST(Password, EmptyPassword) {
    const auto hashed = security::hashPassword("");
    EXPECT_TRUE(security::verifyPassword("", hashed));
    EXPECT_FALSE(security::verifyPassword("not-empty", hashed));
}

TEST(Password, LongPassword) {
    const std::string longPass(1024, 'x');
    const auto hashed = security::hashPassword(longPass);
    EXPECT_TRUE(security::verifyPassword(longPass, hashed));
    EXPECT_FALSE(security::verifyPassword(longPass + "y", hashed));
}

TEST(Password, UnicodePassword) {
    const auto hashed = security::hashPassword("密码测试🔐");
    EXPECT_TRUE(security::verifyPassword("密码测试🔐", hashed));
    EXPECT_FALSE(security::verifyPassword("密码测试", hashed));
}
