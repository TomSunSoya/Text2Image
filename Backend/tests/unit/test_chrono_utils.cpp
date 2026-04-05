#include <gtest/gtest.h>

#include "utils/chrono_utils.h"

TEST(ChronoUtils, ToDbStringFormatsMysqlDatetime) {
    using namespace std::chrono_literals;

    const auto parsed = utils::chrono::fromDbString("2026-04-05 12:34:56");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(utils::chrono::toDbString(*parsed + 789ms), "2026-04-05 12:34:56");
}

TEST(ChronoUtils, FromDbStringParsesStandardDatetime) {
    const auto parsed = utils::chrono::fromDbString("2026-04-05 12:34:56");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(utils::chrono::toDbString(*parsed), "2026-04-05 12:34:56");
}

TEST(ChronoUtils, FromDbStringAcceptsIsoSeparatorAndFractionalSeconds) {
    const auto parsed = utils::chrono::fromDbString("2026-04-05T12:34:56.789");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(utils::chrono::toDbString(*parsed), "2026-04-05 12:34:56");
}

TEST(ChronoUtils, FromDbStringRejectsInvalidInput) {
    EXPECT_FALSE(utils::chrono::fromDbString("").has_value());
    EXPECT_FALSE(utils::chrono::fromDbString("not-a-time").has_value());
}
