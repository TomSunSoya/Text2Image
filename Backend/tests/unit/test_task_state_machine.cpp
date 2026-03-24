#include <gtest/gtest.h>
#include "task_state_machine.h"

// ==================== isTerminal ====================

TEST(TaskState_IsTerminal, TerminalStates) {
    EXPECT_TRUE(task_state::isTerminal("success"));
    EXPECT_TRUE(task_state::isTerminal("failed"));
    EXPECT_TRUE(task_state::isTerminal("cancelled"));
    EXPECT_TRUE(task_state::isTerminal("timeout"));
}

TEST(TaskState_IsTerminal, NonTerminalStates) {
    EXPECT_FALSE(task_state::isTerminal("queued"));
    EXPECT_FALSE(task_state::isTerminal("pending"));
    EXPECT_FALSE(task_state::isTerminal("generating"));
}

TEST(TaskState_IsTerminal, EmptyAndUnknown) {
    EXPECT_FALSE(task_state::isTerminal(""));
    EXPECT_FALSE(task_state::isTerminal("unknown"));
    EXPECT_FALSE(task_state::isTerminal("SUCCESS")); // case-sensitive
}

// ==================== canCancel ====================

TEST(TaskState_CanCancel, CancellableStates) {
    EXPECT_TRUE(task_state::canCancel("queued"));
    EXPECT_TRUE(task_state::canCancel("pending"));
    EXPECT_TRUE(task_state::canCancel("generating"));
}

TEST(TaskState_CanCancel, NonCancellableStates) {
    EXPECT_FALSE(task_state::canCancel("success"));
    EXPECT_FALSE(task_state::canCancel("failed"));
    EXPECT_FALSE(task_state::canCancel("cancelled"));
    EXPECT_FALSE(task_state::canCancel("timeout"));
}

TEST(TaskState_CanCancel, EmptyAndUnknown) {
    EXPECT_FALSE(task_state::canCancel(""));
    EXPECT_FALSE(task_state::canCancel("garbage"));
}

// ==================== canRetry ====================

TEST(TaskState_CanRetry, RetryableWithQuota) {
    EXPECT_TRUE(task_state::canRetry("failed", 0, 3));
    EXPECT_TRUE(task_state::canRetry("failed", 2, 3));
    EXPECT_TRUE(task_state::canRetry("timeout", 0, 1));
    EXPECT_TRUE(task_state::canRetry("cancelled", 1, 3));
}

TEST(TaskState_CanRetry, QuotaExhausted) {
    EXPECT_FALSE(task_state::canRetry("failed", 3, 3));
    EXPECT_FALSE(task_state::canRetry("failed", 5, 3));
    EXPECT_FALSE(task_state::canRetry("timeout", 1, 1));
}

TEST(TaskState_CanRetry, NonRetryableStates) {
    EXPECT_FALSE(task_state::canRetry("success", 0, 3));
    EXPECT_FALSE(task_state::canRetry("queued", 0, 3));
    EXPECT_FALSE(task_state::canRetry("pending", 0, 3));
    EXPECT_FALSE(task_state::canRetry("generating", 0, 3));
}

TEST(TaskState_CanRetry, ZeroMaxRetries) {
    EXPECT_FALSE(task_state::canRetry("failed", 0, 0));
}

// ==================== canDelete ====================

TEST(TaskState_CanDelete, DeletableStates) {
    EXPECT_TRUE(task_state::canDelete("success"));
    EXPECT_TRUE(task_state::canDelete("failed"));
    EXPECT_TRUE(task_state::canDelete("cancelled"));
    EXPECT_TRUE(task_state::canDelete("timeout"));
}

TEST(TaskState_CanDelete, NonDeletableStates) {
    EXPECT_FALSE(task_state::canDelete("queued"));
    EXPECT_FALSE(task_state::canDelete("pending"));
    EXPECT_FALSE(task_state::canDelete("generating"));
}

// ==================== canReturnBinary ====================

TEST(TaskState_CanReturnBinary, SuccessWithKey) {
    EXPECT_TRUE(task_state::canReturnBinary("success", "task-1.png"));
    EXPECT_TRUE(task_state::canReturnBinary("success", "any-key"));
}

TEST(TaskState_CanReturnBinary, SuccessWithoutKey) {
    EXPECT_FALSE(task_state::canReturnBinary("success", ""));
}

TEST(TaskState_CanReturnBinary, NonSuccessWithKey) {
    EXPECT_FALSE(task_state::canReturnBinary("failed", "task-1.png"));
    EXPECT_FALSE(task_state::canReturnBinary("queued", "task-1.png"));
    EXPECT_FALSE(task_state::canReturnBinary("generating", "task-1.png"));
    EXPECT_FALSE(task_state::canReturnBinary("cancelled", "task-1.png"));
}
