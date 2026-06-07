#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

// Test the ranges transformation pattern used in redis_client.cpp::rebuildTaskQueue
// This tests the int64_t -> std::string conversion via std::ranges::transform.

namespace {

std::vector<std::string> taskIdsToStrings(const std::vector<int64_t>& taskIds) {
    std::vector<std::string> args;
    args.reserve(taskIds.size());
    std::ranges::transform(taskIds, std::back_inserter(args),
                           [](int64_t id) { return std::to_string(id); });
    return args;
}

} // namespace

TEST(RedisClientRanges, ConvertsIntVectorToStringVector) {
    std::vector<int64_t> taskIds = {1, 2, 3};

    const auto args = taskIdsToStrings(taskIds);

    ASSERT_EQ(args.size(), 3);
    EXPECT_EQ(args[0], "1");
    EXPECT_EQ(args[1], "2");
    EXPECT_EQ(args[2], "3");
}

TEST(RedisClientRanges, EmptyVectorReturnsEmpty) {
    std::vector<int64_t> taskIds;

    const auto args = taskIdsToStrings(taskIds);

    EXPECT_TRUE(args.empty());
}

TEST(RedisClientRanges, SingleElement) {
    std::vector<int64_t> taskIds = {42};

    const auto args = taskIdsToStrings(taskIds);

    ASSERT_EQ(args.size(), 1);
    EXPECT_EQ(args[0], "42");
}
