#include <gtest/gtest.h>
#include "models/task_status.h"

// ==================== isTerminal ====================

TEST(TaskState_IsTerminal, TerminalStates) {
    EXPECT_TRUE(models::isTerminal(models::TaskStatus::Success));
    EXPECT_TRUE(models::isTerminal(models::TaskStatus::Failed));
    EXPECT_TRUE(models::isTerminal(models::TaskStatus::Cancelled));
    EXPECT_TRUE(models::isTerminal(models::TaskStatus::Timeout));
}

TEST(TaskState_IsTerminal, NonTerminalStates) {
    EXPECT_FALSE(models::isTerminal(models::TaskStatus::Queued));
    EXPECT_FALSE(models::isTerminal(models::TaskStatus::Pending));
    EXPECT_FALSE(models::isTerminal(models::TaskStatus::Generating));
}

TEST(TaskState_IsTerminal, EmptyAndUnknown) {
    EXPECT_FALSE(models::isTerminal(models::TaskStatus::Unknown));
}

// ==================== canCancel ====================

TEST(TaskState_CanCancel, CancellableStates) {
    EXPECT_TRUE(models::canCancel(models::TaskStatus::Queued));
    EXPECT_TRUE(models::canCancel(models::TaskStatus::Pending));
    EXPECT_TRUE(models::canCancel(models::TaskStatus::Generating));
}

TEST(TaskState_CanCancel, NonCancellableStates) {
    EXPECT_FALSE(models::canCancel(models::TaskStatus::Success));
    EXPECT_FALSE(models::canCancel(models::TaskStatus::Failed));
    EXPECT_FALSE(models::canCancel(models::TaskStatus::Cancelled));
    EXPECT_FALSE(models::canCancel(models::TaskStatus::Timeout));
    EXPECT_FALSE(models::canCancel(models::TaskStatus::Unknown));
}

// ==================== canRetry ====================

TEST(TaskState_CanRetry, RetryableWithQuota) {
    EXPECT_TRUE(models::canRetry(models::TaskStatus::Failed, 0, 3));
    EXPECT_TRUE(models::canRetry(models::TaskStatus::Failed, 2, 3));
    EXPECT_TRUE(models::canRetry(models::TaskStatus::Timeout, 0, 1));
    EXPECT_TRUE(models::canRetry(models::TaskStatus::Cancelled, 1, 3));
}

TEST(TaskState_CanRetry, QuotaExhausted) {
    EXPECT_FALSE(models::canRetry(models::TaskStatus::Failed, 3, 3));
    EXPECT_FALSE(models::canRetry(models::TaskStatus::Failed, 5, 3));
    EXPECT_FALSE(models::canRetry(models::TaskStatus::Timeout, 1, 1));
}

TEST(TaskState_CanRetry, NonRetryableStates) {
    EXPECT_FALSE(models::canRetry(models::TaskStatus::Success, 0, 3));
    EXPECT_FALSE(models::canRetry(models::TaskStatus::Queued, 0, 3));
    EXPECT_FALSE(models::canRetry(models::TaskStatus::Pending, 0, 3));
    EXPECT_FALSE(models::canRetry(models::TaskStatus::Generating, 0, 3));
    EXPECT_FALSE(models::canRetry(models::TaskStatus::Unknown, 0, 3));
}

TEST(TaskState_CanRetry, ZeroMaxRetries) {
    EXPECT_FALSE(models::canRetry(models::TaskStatus::Failed, 0, 0));
}

// ==================== canDelete ====================

TEST(TaskState_CanDelete, DeletableStates) {
    EXPECT_TRUE(models::canDelete(models::TaskStatus::Success));
    EXPECT_TRUE(models::canDelete(models::TaskStatus::Failed));
    EXPECT_TRUE(models::canDelete(models::TaskStatus::Cancelled));
    EXPECT_TRUE(models::canDelete(models::TaskStatus::Timeout));
}

TEST(TaskState_CanDelete, NonDeletableStates) {
    EXPECT_FALSE(models::canDelete(models::TaskStatus::Queued));
    EXPECT_FALSE(models::canDelete(models::TaskStatus::Pending));
    EXPECT_FALSE(models::canDelete(models::TaskStatus::Generating));
    EXPECT_FALSE(models::canDelete(models::TaskStatus::Unknown));
}

// ==================== canReturnBinary ====================

TEST(TaskState_CanReturnBinary, SuccessWithKey) {
    EXPECT_TRUE(models::canReturnBinary(models::TaskStatus::Success, "task-1.png"));
    EXPECT_TRUE(models::canReturnBinary(models::TaskStatus::Success, "any-key"));
}

TEST(TaskState_CanReturnBinary, SuccessWithoutKey) {
    EXPECT_FALSE(models::canReturnBinary(models::TaskStatus::Success, ""));
}

TEST(TaskState_CanReturnBinary, NonSuccessWithKey) {
    EXPECT_FALSE(models::canReturnBinary(models::TaskStatus::Failed, "task-1.png"));
    EXPECT_FALSE(models::canReturnBinary(models::TaskStatus::Queued, "task-1.png"));
    EXPECT_FALSE(models::canReturnBinary(models::TaskStatus::Generating, "task-1.png"));
    EXPECT_FALSE(models::canReturnBinary(models::TaskStatus::Cancelled, "task-1.png"));
    EXPECT_FALSE(models::canReturnBinary(models::TaskStatus::Unknown, "task-1.png"));
}

// ==================== string conversion ====================

TEST(TaskState_StringConversion, KnownStringsRoundTrip) {
    using models::TaskStatus;

    EXPECT_EQ(models::statusFromString("pending"), TaskStatus::Pending);
    EXPECT_EQ(models::statusFromString("queued"), TaskStatus::Queued);
    EXPECT_EQ(models::statusFromString("generating"), TaskStatus::Generating);
    EXPECT_EQ(models::statusFromString("success"), TaskStatus::Success);
    EXPECT_EQ(models::statusFromString("failed"), TaskStatus::Failed);
    EXPECT_EQ(models::statusFromString("cancelled"), TaskStatus::Cancelled);
    EXPECT_EQ(models::statusFromString("timeout"), TaskStatus::Timeout);

    EXPECT_EQ(models::statusToString(TaskStatus::Pending), "pending");
    EXPECT_EQ(models::statusToString(TaskStatus::Queued), "queued");
    EXPECT_EQ(models::statusToString(TaskStatus::Generating), "generating");
    EXPECT_EQ(models::statusToString(TaskStatus::Success), "success");
    EXPECT_EQ(models::statusToString(TaskStatus::Failed), "failed");
    EXPECT_EQ(models::statusToString(TaskStatus::Cancelled), "cancelled");
    EXPECT_EQ(models::statusToString(TaskStatus::Timeout), "timeout");
    EXPECT_EQ(models::statusToString(TaskStatus::Unknown), "unknown");
}

TEST(TaskState_StringConversion, UnknownStringMapsToUnknown) {
    EXPECT_EQ(models::statusFromString(""), models::TaskStatus::Unknown);
    EXPECT_EQ(models::statusFromString("done"), models::TaskStatus::Unknown);
    EXPECT_EQ(models::statusFromString("SUCCESS"), models::TaskStatus::Unknown);
}
