#include <gtest/gtest.h>

#include <stdexcept>

#include "auth_service.h"
#include "image_service.h"
#include "test_db_support.h"

namespace {

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

class ImageFlowTest : public ::testing::Test {
  protected:
    int64_t userId_ = 0;

    void SetUp() override {
        test_support::ensureTestDatabase();
        test_support::cleanTables();
        userId_ = createTestUser("imguser");
    }

    void TearDown() override {
        test_support::cleanTables();
    }
};

// ==================== create ====================

TEST_F(ImageFlowTest, CreateReturnsQueued) {
    ImageService service;
    auto result = service.create(userId_, {{"prompt", "a dog"}});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.status, "queued");
    EXPECT_GT(result->generation.id, 0);
    EXPECT_FALSE(result->generation.request_id.empty());
}

TEST_F(ImageFlowTest, CreateWithAllParameters) {
    ImageService service;
    nlohmann::json payload = {{"prompt", "a cat"}, {"negative_prompt", "blurry"},
                              {"num_steps", 20},   {"height", 512},
                              {"width", 512},      {"seed", 42}};
    auto result = service.create(userId_, payload);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.prompt, "a cat");
    EXPECT_EQ(result->generation.negative_prompt, "blurry");
    EXPECT_EQ(result->generation.num_steps, 20);
}

TEST_F(ImageFlowTest, CreateEmptyPromptFails) {
    ImageService service;
    auto result = service.create(userId_, {{"prompt", "   "}});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "prompt_required");
}

TEST_F(ImageFlowTest, CreateTooShortPromptFails) {
    ImageService service;
    auto result = service.create(userId_, {{"prompt", "ab"}});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "invalid_prompt_length");
}

TEST_F(ImageFlowTest, CreateUnauthorizedFails) {
    ImageService service;
    auto result = service.create(0, {{"prompt", "test"}});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "unauthorized");
}

TEST_F(ImageFlowTest, IdempotentCreate) {
    ImageService service;
    nlohmann::json payload = {{"prompt", "cat"}, {"request_id", "dedup-001"}};

    auto first = service.create(userId_, payload);
    auto second = service.create(userId_, payload);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->generation.id, second->generation.id);
}

// ==================== list ====================

TEST_F(ImageFlowTest, ListMyReturnsCreatedTasks) {
    ImageService service;
    (void)service.create(userId_, {{"prompt", "cat"}});
    (void)service.create(userId_, {{"prompt", "dog"}});

    auto list = service.listMy(userId_, 0, 10);
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(list->total_elements, 2);
    EXPECT_EQ(list->content.size(), 2u);
}

TEST_F(ImageFlowTest, ListMyPagination) {
    ImageService service;
    for (int i = 0; i < 5; ++i) {
        (void)service.create(userId_, {{"prompt", "task " + std::to_string(i)}});
    }

    auto page0 = service.listMy(userId_, 0, 2);
    ASSERT_TRUE(page0.has_value());
    EXPECT_EQ(page0->content.size(), 2u);
    EXPECT_EQ(page0->total_elements, 5);

    auto page1 = service.listMy(userId_, 1, 2);
    ASSERT_TRUE(page1.has_value());
    EXPECT_EQ(page1->content.size(), 2u);
}

TEST_F(ImageFlowTest, ListMyByStatus) {
    ImageService service;
    (void)service.create(userId_, {{"prompt", "ant"}});
    (void)service.create(userId_, {{"prompt", "bee"}});

    auto queued = service.listMyByStatus(userId_, "queued", 0, 10);
    ASSERT_TRUE(queued.has_value());
    EXPECT_EQ(queued->total_elements, 2);
}

TEST_F(ImageFlowTest, ListMyEmptyResult) {
    ImageService service;
    auto list = service.listMy(userId_, 0, 10);
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(list->total_elements, 0);
    EXPECT_TRUE(list->content.empty());
}

// ==================== getById ====================

TEST_F(ImageFlowTest, GetByIdSuccess) {
    ImageService service;
    auto created = service.create(userId_, {{"prompt", "hello"}});
    ASSERT_TRUE(created.has_value());

    auto fetched = service.getById(userId_, created->generation.id, false);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->generation.prompt, "hello");
    EXPECT_EQ(fetched->generation.status, "queued");
}

TEST_F(ImageFlowTest, GetByIdNotFound) {
    ImageService service;
    auto result = service.getById(userId_, 999999, false);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "image_not_found");
}

TEST_F(ImageFlowTest, OtherUserCannotAccessMyTask) {
    ImageService service;
    auto created = service.create(userId_, {{"prompt", "private"}});
    ASSERT_TRUE(created.has_value());

    int64_t otherUserId = createTestUser("otheruser");
    auto result = service.getById(otherUserId, created->generation.id, false);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "image_not_found");
}

// ==================== cancel ====================

TEST_F(ImageFlowTest, CancelQueuedTaskSucceeds) {
    ImageService service;
    auto created = service.create(userId_, {{"prompt", "test"}});
    ASSERT_TRUE(created.has_value());

    auto cancelled = service.cancelById(userId_, created->generation.id);
    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->generation.status, "cancelled");
}

TEST_F(ImageFlowTest, CancelAlreadyCancelledFails) {
    ImageService service;
    auto created = service.create(userId_, {{"prompt", "test"}});
    auto taskId = created->generation.id;

    ASSERT_TRUE(service.cancelById(userId_, taskId).has_value());

    auto again = service.cancelById(userId_, taskId);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, "task_cancel_not_allowed");
}

TEST_F(ImageFlowTest, CancelNonexistentTaskFails) {
    ImageService service;
    auto result = service.cancelById(userId_, 999999);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "task_not_found");
}

// ==================== retry ====================

TEST_F(ImageFlowTest, RetryCancelledTaskSucceeds) {
    ImageService service;
    auto created = service.create(userId_, {{"prompt", "test"}});
    auto taskId = created->generation.id;

    ASSERT_TRUE(service.cancelById(userId_, taskId).has_value());

    auto retried = service.retryById(userId_, taskId);
    ASSERT_TRUE(retried.has_value());
    EXPECT_EQ(retried->generation.status, "queued");
}

TEST_F(ImageFlowTest, RetryQueuedTaskFails) {
    ImageService service;
    auto created = service.create(userId_, {{"prompt", "test"}});

    auto result = service.retryById(userId_, created->generation.id);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "task_retry_not_allowed");
}

// ==================== delete ====================

TEST_F(ImageFlowTest, DeleteCancelledTask) {
    ImageService service;
    auto created = service.create(userId_, {{"prompt", "test"}});
    auto taskId = created->generation.id;

    ASSERT_TRUE(service.cancelById(userId_, taskId).has_value());

    auto deleted = service.deleteById(userId_, taskId);
    ASSERT_TRUE(deleted.has_value());

    // verify it's gone
    auto fetched = service.getById(userId_, taskId, false);
    EXPECT_FALSE(fetched.has_value());
}

TEST_F(ImageFlowTest, DeleteNonexistentFails) {
    ImageService service;
    auto result = service.deleteById(userId_, 999999);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "image_not_found");
}
