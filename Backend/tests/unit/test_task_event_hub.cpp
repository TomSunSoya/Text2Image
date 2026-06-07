#include <gtest/gtest.h>
#include "models/image_generation.h"
#include "services/task_event_hub.h"

TEST(TaskEventHub, SubscribeNullConnectionSafe) {
    auto& hub = TaskEventHub::instance();
    EXPECT_NO_THROW(hub.subscribe(1, nullptr));
}

TEST(TaskEventHub, UnsubscribeNullConnectionSafe) {
    auto& hub = TaskEventHub::instance();
    EXPECT_NO_THROW(hub.unsubscribe(nullptr));
}

TEST(TaskEventHub, PublishWithoutSubscribersSafe) {
    auto& hub = TaskEventHub::instance();
    models::ImageGeneration gen;
    gen.user_id = 99999; // no subscribers for this user
    gen.id = 1;
    EXPECT_NO_THROW(hub.publishTaskUpdated(gen));
}

TEST(TaskEventHub, PublishWithZeroUserIdSafe) {
    auto& hub = TaskEventHub::instance();
    models::ImageGeneration gen;
    gen.user_id = 0; // guard: if (user_id <= 0) return;
    EXPECT_NO_THROW(hub.publishTaskUpdated(gen));
}

TEST(TaskEventHub, SingletonReturnsSameInstance) {
    auto& hub1 = TaskEventHub::instance();
    auto& hub2 = TaskEventHub::instance();
    EXPECT_EQ(&hub1, &hub2);
}