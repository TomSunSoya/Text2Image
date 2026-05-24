#include <gtest/gtest.h>

#include <chrono>

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

#include "Backend.h"
#include "utils/jwt_utils.h"

namespace {
using JwtTraits = jwt::traits::nlohmann_json;

std::string createLegacyTokenWithoutRole(int64_t userId, const std::string& username) {
    const auto& secret =
        backend::cachedConfig().at("jwt").at("secret").get_ref<const std::string&>();
    return jwt::create<jwt::default_clock, JwtTraits>(jwt::default_clock{})
        .set_issuer("backend")
        .set_payload_claim("uid",
                           JwtTraits::value_type(static_cast<JwtTraits::integer_type>(userId)))
        .set_payload_claim("username", JwtTraits::value_type(username))
        .sign(jwt::algorithm::hs256{secret});
}

std::string createExpiredAccessToken() {
    const auto& secret =
        backend::cachedConfig().at("jwt").at("secret").get_ref<const std::string&>();
    return jwt::create<jwt::default_clock, JwtTraits>(jwt::default_clock{})
        .set_issuer("backend")
        .set_expires_at(std::chrono::system_clock::now() - std::chrono::seconds(60))
        .set_payload_claim("uid", JwtTraits::value_type(static_cast<JwtTraits::integer_type>(42)))
        .set_payload_claim("username", JwtTraits::value_type("expired"))
        .set_payload_claim("role", JwtTraits::value_type("user"))
        .set_payload_claim("type", JwtTraits::value_type("access"))
        .set_payload_claim("jti", JwtTraits::value_type("expired-jti"))
        .sign(jwt::algorithm::hs256{secret});
}

std::string createExpiredRefreshToken() {
    const auto& secret =
        backend::cachedConfig().at("jwt").at("secret").get_ref<const std::string&>();
    return jwt::create<jwt::default_clock, JwtTraits>(jwt::default_clock{})
        .set_issuer("backend")
        .set_expires_at(std::chrono::system_clock::now() - std::chrono::seconds(60))
        .set_payload_claim("uid", JwtTraits::value_type(static_cast<JwtTraits::integer_type>(42)))
        .set_payload_claim("type", JwtTraits::value_type("refresh"))
        .set_payload_claim("jti", JwtTraits::value_type("expired-refresh-jti"))
        .sign(jwt::algorithm::hs256{secret});
}
} // namespace

TEST(JWT, CreateAndVerifyRoundTrip) {
    auto token = utils::createToken(42, "testuser");
    EXPECT_FALSE(token.empty());

    auto payload = utils::verifyToken(token);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->user_id, 42);
    EXPECT_EQ(payload->username, "testuser");
    EXPECT_EQ(payload->role, "user");
    EXPECT_FALSE(payload->jti.empty());
}

TEST(JWT, CreateAndVerifyAdminRoleRoundTrip) {
    auto token = utils::createToken(42, "adminuser", "admin");
    EXPECT_FALSE(token.empty());

    auto payload = utils::verifyToken(token);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->user_id, 42);
    EXPECT_EQ(payload->username, "adminuser");
    EXPECT_EQ(payload->role, "admin");
}

TEST(JWT, MissingRoleDefaultsToUser) {
    auto token = createLegacyTokenWithoutRole(42, "legacyuser");

    auto payload = utils::verifyToken(token);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->user_id, 42);
    EXPECT_EQ(payload->username, "legacyuser");
    EXPECT_EQ(payload->role, "user");
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

TEST(JWT, ExpiredAccessTokenReturnsNullopt) {
    EXPECT_FALSE(utils::verifyToken(createExpiredAccessToken()).has_value());
}

TEST(JWT, RefreshTokenRoundTrip) {
    const auto jti = utils::generateJti();
    const auto token = utils::issueRefreshToken(42, jti);

    const auto claims = utils::verifyRefreshToken(token);

    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->user_id, 42);
    EXPECT_EQ(claims->jti, jti);
}

TEST(JWT, ExpiredRefreshTokenReturnsUnauthorized) {
    const auto claims = utils::verifyRefreshToken(createExpiredRefreshToken());

    ASSERT_FALSE(claims.has_value());
    EXPECT_EQ(claims.error().status, drogon::k401Unauthorized);
    EXPECT_EQ(claims.error().code, "invalid_refresh_token");
}

TEST(JWT, AccessTokenIsNotAcceptedAsRefreshToken) {
    const auto claims = utils::verifyRefreshToken(utils::createToken(42, "not-refresh"));

    ASSERT_FALSE(claims.has_value());
    EXPECT_EQ(claims.error().code, "invalid_refresh_token");
}
