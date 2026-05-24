#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "database/ImageRepo.h"
#include "models/failure_code.h"
#include "services/auth_service.h"
#include "services/image_service.h"
#include "test_db_support.h"

namespace {

void setEnvVar(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

int64_t createTestUser(const std::string& username) {
    AuthService auth;
    auto result = auth.registerUser({{"username", username},
                                     {"email", username + "@test.com"},
                                     {"password", "pass123456"},
                                     {"nickname", username}});
    if (!result.has_value()) {
        throw std::runtime_error(result.error().message);
    }
    return result->user.id;
}

} // namespace

TEST(TaskEngineLazyBootstrap, EnqueueStartsWorkersWithoutExplicitBootstrap) {
    setEnvVar("TASK_ENGINE_WORKERS", "1");
    setEnvVar("TASK_ENGINE_POLL_INTERVAL_MS", "100");
    setEnvVar("PYTHON_SERVICE_URL", "http://127.0.0.1:9");
    setEnvVar("PYTHON_SERVICE_TIMEOUT_SECONDS", "1");

    test_support::ensureTestDatabase();
    test_support::cleanTables();

    const auto userId = createTestUser("lazybootstrap");

    ImageService service;
    auto created = service.create(userId, {{"prompt", "lazy bootstrap regression"}});
    ASSERT_TRUE(created.has_value());

    ImageRepo repo;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    std::optional<models::ImageGeneration> lastTask;
    while (std::chrono::steady_clock::now() < deadline) {
        auto task = repo.findByIdAndUserId(created->generation.id, userId);
        ASSERT_TRUE(task.has_value()) << task.error().message;
        ASSERT_TRUE(task->has_value());

        lastTask = **task;
        if (lastTask->status == models::TaskStatus::Queued && lastTask->retry_count > 0) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ASSERT_TRUE(lastTask.has_value());
    EXPECT_EQ(lastTask->status, models::TaskStatus::Queued);
    EXPECT_GT(lastTask->retry_count, 0);
    EXPECT_EQ(lastTask->failure_code, std::string(models::failure::kModelServiceUnavailable));
    EXPECT_TRUE(lastTask->worker_id.empty());
    EXPECT_FALSE(lastTask->lease_expires_at.has_value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    std::fflush(nullptr);
    std::_Exit(result);
}
