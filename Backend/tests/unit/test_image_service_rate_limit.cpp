#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "models/task_status.h"
#include "services/image_service.h"
#include "image_service_test_fakes.h"

namespace {

using image_service_test_fakes::FakeImageRepo;
using image_service_test_fakes::FakeImageStorage;
using image_service_test_fakes::makeImage;
using image_service_test_fakes::makeService;
using image_service_test_fakes::SpyCacheClient;

nlohmann::json validPayload(std::string requestId = "rate-limit-request") {
    return {{"requestId", std::move(requestId)},
            {"prompt", "a valid image generation prompt"},
            {"negativePrompt", ""},
            {"numSteps", 8},
            {"height", 768},
            {"width", 768}};
}

void addActiveTasks(FakeImageRepo& repo, int64_t userId, int count) {
    for (int i = 0; i < count; ++i) {
        repo.images[{100 + i, userId}] = makeImage(100 + i, userId, models::TaskStatus::Queued);
    }
}

} // namespace

TEST(ImageServiceRateLimit, ActiveTaskLimitReturnsTooManyRequests) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    addActiveTasks(*repo, 7, 3);

    auto service = makeService(repo, storage, cache);
    const auto result = service.create(7, validPayload());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k429TooManyRequests);
    EXPECT_EQ(result.error().code, "too_many_active_tasks");
    EXPECT_EQ(repo->count_active_calls, 1);
    EXPECT_EQ(repo->insert_calls, 0);
}

TEST(ImageServiceRateLimit, ActiveTaskLimitIsPerUser) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    addActiveTasks(*repo, 8, 3);

    auto service = makeService(repo, storage, cache);
    const auto result = service.create(7, validPayload());

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(repo->count_active_calls, 1);
    EXPECT_EQ(repo->insert_calls, 1);
}

TEST(ImageServiceRateLimit, AdminBypassesActiveTaskLimit) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    addActiveTasks(*repo, 7, 3);

    auto service = makeService(repo, storage, cache);
    const auto result = service.create(7, validPayload(), true);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(repo->count_active_calls, 0);
    EXPECT_EQ(repo->insert_calls, 1);
}

TEST(ImageServiceRateLimit, ActiveTaskCountRepoErrorMapsToServiceError) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->next_error = RepoError{RepoError::Kind::DbUnavailable, "mysql is down"};

    auto service = makeService(repo, storage, cache);
    const auto result = service.create(7, validPayload());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k503ServiceUnavailable);
    EXPECT_EQ(result.error().code, "database_unavailable");
    EXPECT_EQ(repo->insert_calls, 0);
}
