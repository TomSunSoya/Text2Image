#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>

#include "database/ImageRepo.h"
#include "test_db_support.h"

namespace {

constexpr int64_t kTestUserId = 42;

models::ImageGeneration makeGen(int64_t userId, const std::string& prompt,
                                models::TaskStatus status = models::TaskStatus::Queued) {
    models::ImageGeneration gen;
    gen.user_id = userId;
    gen.prompt = prompt;
    gen.status = status;
    gen.negative_prompt = "test np";
    gen.num_steps = 20;
    gen.height = 512;
    gen.width = 512;
    gen.seed = 12345;
    gen.max_retries = 3;
    gen.created_at = std::chrono::system_clock::now();
    return gen;
}

} // namespace

class ImageRepoTest : public ::testing::Test {
  protected:
    ImageRepo repo_;

    void SetUp() override {
        test_support::ensureTestDatabase();
        test_support::cleanTables();
    }

    void TearDown() override {
        test_support::cleanTables();
    }
};

// ==================== Insert + FindById ====================

TEST_F(ImageRepoTest, InsertAndFindByIdRoundTrip) {
    auto gen = makeGen(kTestUserId, "round-trip test");
    gen.request_id = "req-001";

    int64_t id = repo_.insert(gen);
    EXPECT_GT(id, 0);

    auto fetched = repo_.findByIdAndUserId(id, kTestUserId);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->id, id);
    EXPECT_EQ(fetched->user_id, kTestUserId);
    EXPECT_EQ(fetched->prompt, "round-trip test");
    EXPECT_EQ(fetched->negative_prompt, "test np");
    EXPECT_EQ(fetched->num_steps, 20);
    EXPECT_EQ(fetched->height, 512);
    EXPECT_EQ(fetched->width, 512);
    ASSERT_TRUE(fetched->seed.has_value());
    EXPECT_EQ(fetched->seed.value(), 12345);
    EXPECT_EQ(fetched->status, models::TaskStatus::Queued);
    EXPECT_EQ(fetched->retry_count, 0);
    EXPECT_EQ(fetched->max_retries, 3);
    EXPECT_EQ(fetched->request_id, "req-001");
    EXPECT_TRUE(fetched->worker_id.empty());
}

TEST_F(ImageRepoTest, FindByIdWrongUserReturnsNullopt) {
    auto gen = makeGen(kTestUserId, "private task");
    int64_t id = repo_.insert(gen);

    auto fetched = repo_.findByIdAndUserId(id, kTestUserId + 1);
    EXPECT_FALSE(fetched.has_value());
}

TEST_F(ImageRepoTest, FindByIdNonexistentReturnsNullopt) {
    auto fetched = repo_.findByIdAndUserId(999999, kTestUserId);
    EXPECT_FALSE(fetched.has_value());
}

// ==================== FindByUserId Pagination ====================

TEST_F(ImageRepoTest, FindByUserIdPaginated) {
    for (int i = 0; i < 5; ++i) {
        auto gen = makeGen(kTestUserId, "task " + std::to_string(i));
        repo_.insert(gen);
    }

    auto page0 = repo_.findByUserId(kTestUserId, 0, 3);
    EXPECT_EQ(page0.content.size(), 3u);
    EXPECT_EQ(page0.total_elements, 5);

    // ORDER BY id DESC, so highest id first
    EXPECT_GT(page0.content[0].id, page0.content[1].id);
    EXPECT_GT(page0.content[1].id, page0.content[2].id);

    auto page1 = repo_.findByUserId(kTestUserId, 1, 3);
    EXPECT_EQ(page1.content.size(), 2u);
    EXPECT_EQ(page1.total_elements, 5);
}

TEST_F(ImageRepoTest, FindByUserIdEmptyResult) {
    auto page = repo_.findByUserId(kTestUserId, 0, 10);
    EXPECT_EQ(page.content.size(), 0u);
    EXPECT_EQ(page.total_elements, 0);
}

TEST_F(ImageRepoTest, FindByUserIdDifferentUsersIsolated) {
    auto gen1 = makeGen(kTestUserId, "user 42 task");
    repo_.insert(gen1);
    auto gen2 = makeGen(kTestUserId + 1, "user 43 task");
    repo_.insert(gen2);

    auto page42 = repo_.findByUserId(kTestUserId, 0, 10);
    EXPECT_EQ(page42.total_elements, 1);
    EXPECT_EQ(page42.content[0].prompt, "user 42 task");

    auto page43 = repo_.findByUserId(kTestUserId + 1, 0, 10);
    EXPECT_EQ(page43.total_elements, 1);
    EXPECT_EQ(page43.content[0].prompt, "user 43 task");
}

// ==================== FindByUserIdAndStatus ====================

TEST_F(ImageRepoTest, FindByUserIdAndStatusFilter) {
    repo_.insert(makeGen(kTestUserId, "q1", models::TaskStatus::Queued));
    repo_.insert(makeGen(kTestUserId, "q2", models::TaskStatus::Queued));
    repo_.insert(makeGen(kTestUserId, "ok", models::TaskStatus::Success));
    repo_.insert(makeGen(kTestUserId, "fail", models::TaskStatus::Failed));

    auto queued = repo_.findByUserIdAndStatus(kTestUserId, models::TaskStatus::Queued, 0, 10);
    EXPECT_EQ(queued.total_elements, 2);
    for (const auto& g : queued.content) {
        EXPECT_EQ(g.status, models::TaskStatus::Queued);
    }

    auto success = repo_.findByUserIdAndStatus(kTestUserId, models::TaskStatus::Success, 0, 10);
    EXPECT_EQ(success.total_elements, 1);
    EXPECT_EQ(success.content[0].prompt, "ok");

    auto cancelled = repo_.findByUserIdAndStatus(kTestUserId, models::TaskStatus::Cancelled, 0, 10);
    EXPECT_EQ(cancelled.total_elements, 0);
}

// ==================== FindByRequestIdAndUserId ====================

TEST_F(ImageRepoTest, FindByRequestIdAndUserIdMatch) {
    auto gen = makeGen(kTestUserId, "dedup test");
    gen.request_id = "dedup-abc";
    int64_t id = repo_.insert(gen);

    auto found = repo_.findByRequestIdAndUserId("dedup-abc", kTestUserId);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, id);
    EXPECT_EQ(found->request_id, "dedup-abc");
}

TEST_F(ImageRepoTest, FindByRequestIdWrongUserReturnsNullopt) {
    auto gen = makeGen(kTestUserId, "dedup test");
    gen.request_id = "dedup-xyz";
    repo_.insert(gen);

    auto found = repo_.findByRequestIdAndUserId("dedup-xyz", kTestUserId + 1);
    EXPECT_FALSE(found.has_value());
}

TEST_F(ImageRepoTest, FindByRequestIdNotFoundReturnsNullopt) {
    auto found = repo_.findByRequestIdAndUserId("nonexistent", kTestUserId);
    EXPECT_FALSE(found.has_value());
}

// ==================== ClaimNextTask ====================

TEST_F(ImageRepoTest, ClaimNextTaskAtomically) {
    repo_.insert(makeGen(kTestUserId, "first"));
    repo_.insert(makeGen(kTestUserId, "second"));
    repo_.insert(makeGen(kTestUserId, "third"));

    auto claimed1 = repo_.claimNextTask("worker1", 60);
    ASSERT_TRUE(claimed1.has_value());
    EXPECT_EQ(claimed1->status, models::TaskStatus::Generating);
    EXPECT_EQ(claimed1->worker_id, "worker1");
    EXPECT_TRUE(claimed1->lease_expires_at.has_value());
    EXPECT_TRUE(claimed1->started_at.has_value());
    EXPECT_EQ(claimed1->prompt, "first"); // oldest queued task

    // Claim second task
    auto claimed2 = repo_.claimNextTask("worker2", 60);
    ASSERT_TRUE(claimed2.has_value());
    EXPECT_EQ(claimed2->status, models::TaskStatus::Generating);
    EXPECT_EQ(claimed2->worker_id, "worker2");
    EXPECT_NE(claimed2->id, claimed1->id);
}

TEST_F(ImageRepoTest, ClaimNextTaskEmptyQueue) {
    auto result = repo_.claimNextTask("worker1", 60);
    EXPECT_FALSE(result.has_value());
}

// ==================== ClaimTaskById ====================

TEST_F(ImageRepoTest, ClaimTaskByIdSuccess) {
    auto gen = makeGen(kTestUserId, "specific task");
    int64_t id = repo_.insert(gen);

    auto claimed = repo_.claimTaskById(id, "worker-xyz", 120);
    ASSERT_TRUE(claimed.has_value());
    EXPECT_EQ(claimed->id, id);
    EXPECT_EQ(claimed->status, models::TaskStatus::Generating);
    EXPECT_EQ(claimed->worker_id, "worker-xyz");
    EXPECT_TRUE(claimed->lease_expires_at.has_value());
    EXPECT_TRUE(claimed->started_at.has_value());
}

TEST_F(ImageRepoTest, ClaimTaskByIdAlreadyClaimedFails) {
    auto gen = makeGen(kTestUserId, "first claim");
    int64_t id = repo_.insert(gen);

    ASSERT_TRUE(repo_.claimTaskById(id, "worker-a", 60).has_value());
    // Second claim should fail — status is now 'generating', not 'queued'/'pending'
    auto secondClaim = repo_.claimTaskById(id, "worker-b", 60);
    EXPECT_FALSE(secondClaim.has_value());
}

TEST_F(ImageRepoTest, ClaimTaskByIdNonexistentFails) {
    auto claimed = repo_.claimTaskById(999999, "worker-x", 60);
    EXPECT_FALSE(claimed.has_value());
}

// ==================== CancelByIdAndUserId ====================

TEST_F(ImageRepoTest, CancelByIdAndUserIdQueued) {
    auto gen = makeGen(kTestUserId, "to cancel");
    int64_t id = repo_.insert(gen);

    models::ImageGeneration updated;
    bool result = repo_.cancelByIdAndUserId(id, kTestUserId, &updated);
    EXPECT_TRUE(result);
    EXPECT_EQ(updated.status, models::TaskStatus::Cancelled);
    EXPECT_TRUE(updated.cancelled_at.has_value());

    // Verify via findByIdAndUserId
    auto fetched = repo_.findByIdAndUserId(id, kTestUserId);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->status, models::TaskStatus::Cancelled);
    EXPECT_TRUE(fetched->cancelled_at.has_value());
    EXPECT_TRUE(fetched->worker_id.empty());
}

TEST_F(ImageRepoTest, CancelByIdAndUserIdWrongUserFails) {
    auto gen = makeGen(kTestUserId, "owned by 42");
    int64_t id = repo_.insert(gen);

    bool result = repo_.cancelByIdAndUserId(id, kTestUserId + 1);
    EXPECT_FALSE(result);
}

TEST_F(ImageRepoTest, CancelByIdAndUserIdAlreadyCancelledFails) {
    auto gen = makeGen(kTestUserId, "cancel twice");
    int64_t id = repo_.insert(gen);

    ASSERT_TRUE(repo_.cancelByIdAndUserId(id, kTestUserId));
    // Second cancel — status is now 'cancelled', not in (queued, pending, generating)
    bool result = repo_.cancelByIdAndUserId(id, kTestUserId);
    EXPECT_FALSE(result);
}

// ==================== RetryByIdAndUserId ====================

TEST_F(ImageRepoTest, RetryByIdAndUserIdFailed) {
    auto gen = makeGen(kTestUserId, "failed task", models::TaskStatus::Failed);
    gen.retry_count = 0;
    gen.max_retries = 3;
    int64_t id = repo_.insert(gen);

    models::ImageGeneration updated;
    bool result = repo_.retryByIdAndUserId(id, kTestUserId, &updated);
    EXPECT_TRUE(result);
    EXPECT_EQ(updated.status, models::TaskStatus::Queued);
    EXPECT_EQ(updated.retry_count, 1);

    auto fetched = repo_.findByIdAndUserId(id, kTestUserId);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->status, models::TaskStatus::Queued);
    EXPECT_EQ(fetched->retry_count, 1);
    EXPECT_TRUE(fetched->image_url.empty());
    EXPECT_TRUE(fetched->worker_id.empty());
}

TEST_F(ImageRepoTest, RetryByIdAndUserIdQueuedFails) {
    auto gen = makeGen(kTestUserId, "queued task", models::TaskStatus::Queued);
    int64_t id = repo_.insert(gen);

    bool result = repo_.retryByIdAndUserId(id, kTestUserId);
    EXPECT_FALSE(result);
}

TEST_F(ImageRepoTest, RetryByIdAndUserIdExceedsMaxRetriesFails) {
    auto gen = makeGen(kTestUserId, "max retries", models::TaskStatus::Failed);
    gen.retry_count = 3; // already at max_retries (default 3)
    gen.max_retries = 3;
    int64_t id = repo_.insert(gen);

    bool result = repo_.retryByIdAndUserId(id, kTestUserId);
    EXPECT_FALSE(result);
}

// ==================== DeleteByIdAndUserId ====================

TEST_F(ImageRepoTest, DeleteByIdAndUserIdTerminalStatus) {
    auto gen = makeGen(kTestUserId, "completed", models::TaskStatus::Success);
    int64_t id = repo_.insert(gen);

    bool result = repo_.deleteByIdAndUserId(id, kTestUserId);
    EXPECT_TRUE(result);

    auto fetched = repo_.findByIdAndUserId(id, kTestUserId);
    EXPECT_FALSE(fetched.has_value());
}

TEST_F(ImageRepoTest, DeleteByIdAndUserIdNonTerminalFails) {
    auto gen = makeGen(kTestUserId, "queued", models::TaskStatus::Queued);
    int64_t id = repo_.insert(gen);

    // deleteByIdAndUserId only deletes terminal statuses
    bool result = repo_.deleteByIdAndUserId(id, kTestUserId);
    EXPECT_FALSE(result);

    auto fetched = repo_.findByIdAndUserId(id, kTestUserId);
    ASSERT_TRUE(fetched.has_value());
}

// ==================== FinishClaimedTask ====================

TEST_F(ImageRepoTest, FinishClaimedTaskLifecycle) {
    // Setup: insert and claim
    auto gen = makeGen(kTestUserId, "lifecycle task");
    int64_t id = repo_.insert(gen);
    auto claimed = repo_.claimTaskById(id, "worker-lifecycle", 300);
    ASSERT_TRUE(claimed.has_value());

    // Build result
    models::ImageGeneration result = *claimed;
    result.status = models::TaskStatus::Success;
    result.image_url = "http://storage/images/result.png";
    result.thumbnail_url = "http://storage/thumbnails/result_thumb.png";
    result.storage_key = "images/result.png";
    result.generation_time = 2.5;
    result.completed_at = std::chrono::system_clock::now();
    result.failure_code = ""; // not a failure

    bool finished = repo_.finishClaimedTask(result);
    EXPECT_TRUE(finished);

    auto fetched = repo_.findByIdAndUserId(id, kTestUserId);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->status, models::TaskStatus::Success);
    EXPECT_EQ(fetched->image_url, "http://storage/images/result.png");
    EXPECT_EQ(fetched->thumbnail_url, "http://storage/thumbnails/result_thumb.png");
    EXPECT_EQ(fetched->storage_key, "images/result.png");
    EXPECT_DOUBLE_EQ(fetched->generation_time, 2.5);
    EXPECT_TRUE(fetched->completed_at.has_value());
    EXPECT_TRUE(fetched->worker_id.empty());
    EXPECT_FALSE(fetched->lease_expires_at.has_value());
}

TEST_F(ImageRepoTest, FinishClaimedTaskWrongWorkerFails) {
    auto gen = makeGen(kTestUserId, "wrong worker");
    int64_t id = repo_.insert(gen);
    auto claimed = repo_.claimTaskById(id, "real-worker", 300);
    ASSERT_TRUE(claimed.has_value());

    models::ImageGeneration result = *claimed;
    result.worker_id = "wrong-worker"; // mismatch
    result.status = models::TaskStatus::Success;

    bool finished = repo_.finishClaimedTask(result);
    EXPECT_FALSE(finished);
}

// ==================== RenewLease ====================

TEST_F(ImageRepoTest, RenewLease) {
    auto gen = makeGen(kTestUserId, "renew test");
    int64_t id = repo_.insert(gen);
    auto claimed = repo_.claimTaskById(id, "worker-lease", 60);
    ASSERT_TRUE(claimed.has_value());

    bool renewed = repo_.renewLease(id, kTestUserId, "worker-lease", 300);
    EXPECT_TRUE(renewed);
}

TEST_F(ImageRepoTest, RenewLeaseWrongWorkerFails) {
    auto gen = makeGen(kTestUserId, "bad renew");
    int64_t id = repo_.insert(gen);
    auto claimed = repo_.claimTaskById(id, "worker-lease", 60);
    ASSERT_TRUE(claimed.has_value());

    bool renewed = repo_.renewLease(id, kTestUserId, "worker-other", 300);
    EXPECT_FALSE(renewed);
}

TEST_F(ImageRepoTest, RenewLeaseNonGeneratingFails) {
    auto gen = makeGen(kTestUserId, "not generating", models::TaskStatus::Queued);
    int64_t id = repo_.insert(gen);

    bool renewed = repo_.renewLease(id, kTestUserId, "worker-x", 60);
    EXPECT_FALSE(renewed);
}

// ==================== FindQueuedTaskIds ====================

TEST_F(ImageRepoTest, FindQueuedTaskIds) {
    repo_.insert(makeGen(kTestUserId, "q1", models::TaskStatus::Queued));
    repo_.insert(makeGen(kTestUserId, "q2", models::TaskStatus::Pending));
    repo_.insert(makeGen(kTestUserId, "ok", models::TaskStatus::Success));
    repo_.insert(makeGen(kTestUserId, "fail", models::TaskStatus::Failed));
    repo_.insert(makeGen(kTestUserId, "gen", models::TaskStatus::Generating));
    repo_.insert(makeGen(kTestUserId, "cancelled", models::TaskStatus::Cancelled));

    auto ids = repo_.findQueuedTaskIds();
    // Only queued and pending should be returned
    EXPECT_EQ(ids.size(), 2u);
    // Ordered by created_at ASC, id ASC
    EXPECT_LT(ids[0], ids[1]);
}

TEST_F(ImageRepoTest, FindQueuedTaskIdsEmpty) {
    auto ids = repo_.findQueuedTaskIds();
    EXPECT_TRUE(ids.empty());
}

// ==================== UpdateStatusAndError ====================

TEST_F(ImageRepoTest, UpdateStatusAndErrorToFailed) {
    auto gen = makeGen(kTestUserId, "will fail");
    int64_t id = repo_.insert(gen);

    bool result =
        repo_.updateStatusAndError(id, kTestUserId, models::TaskStatus::Failed, "model OOM");
    EXPECT_TRUE(result);

    auto fetched = repo_.findByIdAndUserId(id, kTestUserId);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->status, models::TaskStatus::Failed);
    EXPECT_EQ(fetched->error_message, "model OOM");
    EXPECT_TRUE(fetched->completed_at.has_value());
}

TEST_F(ImageRepoTest, UpdateStatusAndErrorWrongUserFails) {
    auto gen = makeGen(kTestUserId, "owned by 42");
    int64_t id = repo_.insert(gen);

    bool result =
        repo_.updateStatusAndError(id, kTestUserId + 1, models::TaskStatus::Failed, "error");
    EXPECT_FALSE(result);
}
