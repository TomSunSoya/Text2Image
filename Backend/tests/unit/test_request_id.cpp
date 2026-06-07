#include <gtest/gtest.h>

#include <regex>
#include <string>
#include <unordered_set>

#include "utils/request_id.h"

TEST(GenerateRequestId, MatchesUuidV4Shape) {
    static const std::regex kUuidV4(
        "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    EXPECT_TRUE(std::regex_match(utils::generateRequestId(), kUuidV4));
}

TEST(GenerateRequestId, ProducesUniqueValues) {
    std::unordered_set<std::string> seen;
    for (int i = 0; i < 1000; ++i) {
        seen.insert(utils::generateRequestId());
    }
    EXPECT_EQ(seen.size(), 1000u);
}

TEST(SanitizeRequestId, AcceptsUuid) {
    const std::string id = "550e8400-e29b-41d4-a716-446655440000";
    EXPECT_EQ(utils::sanitizeRequestId(id), id);
}

TEST(SanitizeRequestId, AcceptsBusinessRequestIdShape) {
    EXPECT_EQ(utils::sanitizeRequestId("img-1700000000-deadbeef"), "img-1700000000-deadbeef");
}

TEST(SanitizeRequestId, TrimsSurroundingWhitespace) {
    EXPECT_EQ(utils::sanitizeRequestId("  abc-123  "), "abc-123");
}

TEST(SanitizeRequestId, RejectsEmptyOrWhitespace) {
    EXPECT_TRUE(utils::sanitizeRequestId("").empty());
    EXPECT_TRUE(utils::sanitizeRequestId("   ").empty());
}

TEST(SanitizeRequestId, RejectsDisallowedCharacters) {
    EXPECT_TRUE(utils::sanitizeRequestId("has space").empty());
    EXPECT_TRUE(utils::sanitizeRequestId("inject\r\nSet-Cookie").empty());
    EXPECT_TRUE(utils::sanitizeRequestId("semi;colon").empty());
}

TEST(SanitizeRequestId, RejectsOverlongValue) {
    EXPECT_TRUE(utils::sanitizeRequestId(std::string(129, 'a')).empty());
    EXPECT_EQ(utils::sanitizeRequestId(std::string(128, 'a')).size(), 128u);
}
