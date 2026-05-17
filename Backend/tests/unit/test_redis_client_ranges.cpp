#include <gtest/gtest.h>
#include <ranges>
#include <string>
#include <vector>

// Test the ranges transformation pattern used in redis_client.cpp::rebuildTaskQueue
// This tests the int64_t -> std::string conversion via views::transform + ranges::to

TEST(RedisClientRanges, ConvertsIntVectorToStringVector) {
    std::vector<int64_t> taskIds = {1, 2, 3};

    auto args = taskIds
        | std::views::transform([](int64_t id) { return std::to_string(id); })
        | std::ranges::to<std::vector<std::string>>();

    ASSERT_EQ(args.size(), 3);
    EXPECT_EQ(args[0], "1");
    EXPECT_EQ(args[1], "2");
    EXPECT_EQ(args[2], "3");
}

TEST(RedisClientRanges, EmptyVectorReturnsEmpty) {
    std::vector<int64_t> taskIds;

    auto args = taskIds
        | std::views::transform([](int64_t id) { return std::to_string(id); })
        | std::ranges::to<std::vector<std::string>>();

    EXPECT_TRUE(args.empty());
}

TEST(RedisClientRanges, SingleElement) {
    std::vector<int64_t> taskIds = {42};

    auto args = taskIds
        | std::views::transform([](int64_t id) { return std::to_string(id); })
        | std::ranges::to<std::vector<std::string>>();

    ASSERT_EQ(args.size(), 1);
    EXPECT_EQ(args[0], "42");
}