#include <gtest/gtest.h>
#include "utils/jwt_utils.h"

TEST(JWT, CreateAndVerifyRoundTrip) {
    auto token = utils::createToken(42, "testuser");
    EXPECT_FALSE(token.empty());

    auto payload = utils::verifyToken(token);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->user_id, 42);
    EXPECT_EQ(payload->username, "testuser");
}

TEST(JWT, DifferentUsersProduceDifferentTokens) {
    auto t1 = utils::createToken(1, "alice");
    auto t2 = utils::createToken(2, "bob");
    EXPECT_NE(t1, t2);
}

TEST(JWT, VerifyPreservesLargeUserId) {
    const int64_t largeId = 9'000'000'000LL;
    auto token = utils::createToken(largeId, "bigid");
    auto payload = utils::verifyToken(token);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->user_id, largeId);
}

TEST(JWT, InvalidTokenReturnsNullopt) {
    EXPECT_FALSE(utils::verifyToken("").has_value());
    EXPECT_FALSE(utils::verifyToken("not.a.jwt").has_value());
    EXPECT_FALSE(utils::verifyToken("abc").has_value());
}

TEST(JWT, TamperedTokenReturnsNullopt) {
    auto token = utils::createToken(1, "user");
    ASSERT_FALSE(token.empty());

    // flip last character
    token.back() = (token.back() == 'a') ? 'b' : 'a';
    EXPECT_FALSE(utils::verifyToken(token).has_value());
}

TEST(JWT, TruncatedTokenReturnsNullopt) {
    auto token = utils::createToken(1, "user");
    token = token.substr(0, token.size() / 2);
    EXPECT_FALSE(utils::verifyToken(token).has_value());
}
