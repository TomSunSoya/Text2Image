#include <gtest/gtest.h>
#include "utils/string_utils.h"

TEST(ParseBool, TruthyTrue) {
    auto result = utils::parseBool("true");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
}

TEST(ParseBool, TruthyUpperCase) {
    auto result = utils::parseBool("TRUE");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
}

TEST(ParseBool, TruthyYesWithWhitespace) {
    auto result = utils::parseBool("  yes  ");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
}

TEST(ParseBool, TruthyOne) {
    auto result = utils::parseBool("1");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
}

TEST(ParseBool, TruthyOn) {
    auto result = utils::parseBool("on");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
}

TEST(ParseBool, TruthyMixedCaseWithWhitespace) {
    auto result = utils::parseBool("  TrUe  ");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());
}

TEST(ParseBool, FalsyFalse) {
    auto result = utils::parseBool("FALSE");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
}

TEST(ParseBool, FalsyZero) {
    auto result = utils::parseBool("0");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
}

TEST(ParseBool, FalsyOff) {
    auto result = utils::parseBool("off");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
}

TEST(ParseBool, FalsyNo) {
    auto result = utils::parseBool("no");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
}

TEST(ParseBool, InvalidStringReturnsNullopt) {
    auto result = utils::parseBool("invalid");
    EXPECT_EQ(result, std::nullopt);
}

TEST(ParseBool, EmptyStringReturnsNullopt) {
    auto result = utils::parseBool("");
    EXPECT_EQ(result, std::nullopt);
}
