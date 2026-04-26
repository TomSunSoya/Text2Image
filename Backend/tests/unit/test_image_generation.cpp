#include <gtest/gtest.h>
#include "models/image_generation.h"
#include "utils/chrono_utils.h"

namespace {

models::ImageGeneration makeFullImageGeneration() {
    models::ImageGeneration image;
    image.id = 42;
    image.user_id = 7;
    image.request_id = "req-roundtrip";
    image.prompt = "prompt";
    image.negative_prompt = "negative";
    image.num_steps = 12;
    image.height = 1024;
    image.width = 768;
    image.seed = 123;
    image.status = models::TaskStatus::Generating;
    image.retry_count = 2;
    image.max_retries = 5;
    image.failure_code = "lease_expired";
    image.worker_id = "worker-1";
    image.image_url = "https://example.test/image.png";
    image.thumbnail_url = "https://example.test/thumb.png";
    image.storage_key = "images/42.png";
    image.image_bytes = "raw-image-bytes";
    image.error_message = "error";
    image.generation_time = 3.5;
    image.created_at = *utils::chrono::fromDbString("2026-04-05 12:34:56");
    image.started_at = *utils::chrono::fromDbString("2026-04-05 12:35:00");
    image.completed_at = *utils::chrono::fromDbString("2026-04-05 12:35:30");
    image.cancelled_at = *utils::chrono::fromDbString("2026-04-05 12:36:00");
    image.lease_expires_at = *utils::chrono::fromDbString("2026-04-05 12:40:00");
    return image;
}

void expectEquivalent(const models::ImageGeneration& actual,
                      const models::ImageGeneration& expected) {
    EXPECT_EQ(actual.id, expected.id);
    EXPECT_EQ(actual.user_id, expected.user_id);
    EXPECT_EQ(actual.request_id, expected.request_id);
    EXPECT_EQ(actual.prompt, expected.prompt);
    EXPECT_EQ(actual.negative_prompt, expected.negative_prompt);
    EXPECT_EQ(actual.num_steps, expected.num_steps);
    EXPECT_EQ(actual.height, expected.height);
    EXPECT_EQ(actual.width, expected.width);
    EXPECT_EQ(actual.seed, expected.seed);
    EXPECT_EQ(actual.status, expected.status);
    EXPECT_EQ(actual.retry_count, expected.retry_count);
    EXPECT_EQ(actual.max_retries, expected.max_retries);
    EXPECT_EQ(actual.failure_code, expected.failure_code);
    EXPECT_EQ(actual.worker_id, expected.worker_id);
    EXPECT_EQ(actual.image_url, expected.image_url);
    EXPECT_EQ(actual.thumbnail_url, expected.thumbnail_url);
    EXPECT_EQ(actual.storage_key, expected.storage_key);
    EXPECT_EQ(actual.image_bytes, expected.image_bytes);
    EXPECT_EQ(actual.error_message, expected.error_message);
    EXPECT_DOUBLE_EQ(actual.generation_time, expected.generation_time);
    EXPECT_EQ(actual.created_at, expected.created_at);
    EXPECT_EQ(actual.started_at, expected.started_at);
    EXPECT_EQ(actual.completed_at, expected.completed_at);
    EXPECT_EQ(actual.cancelled_at, expected.cancelled_at);
    EXPECT_EQ(actual.lease_expires_at, expected.lease_expires_at);
}

} // namespace

// ==================== fromJson ====================

TEST(ImageGeneration_FromJson, BasicFields) {
    nlohmann::json j = {{"prompt", "a cat in space"},
                        {"negative_prompt", "blurry"},
                        {"num_steps", 20},
                        {"height", 512},
                        {"width", 512},
                        {"seed", 42}};

    auto gen = models::ImageGeneration::fromJson(j);
    EXPECT_EQ(gen.prompt, "a cat in space");
    EXPECT_EQ(gen.negative_prompt, "blurry");
    EXPECT_EQ(gen.num_steps, 20);
    EXPECT_EQ(gen.height, 512);
    EXPECT_EQ(gen.width, 512);
    ASSERT_TRUE(gen.seed.has_value());
    EXPECT_EQ(gen.seed.value(), 42);
}

TEST(ImageGeneration_FromJson, CamelCaseKeys) {
    nlohmann::json j = {{"prompt", "test"},
                        {"userId", 99},
                        {"negativePrompt", "ugly"},
                        {"numSteps", 15},
                        {"requestId", "req-1"},
                        {"imageUrl", "http://example.com/img.png"},
                        {"thumbnailUrl", "http://example.com/thumb.png"},
                        {"storageKey", "task-1.png"},
                        {"imageBytes", "bytes"},
                        {"errorMessage", "oops"},
                        {"generationTime", 12.5},
                        {"retryCount", 2},
                        {"maxRetries", 3},
                        {"failureCode", "timeout"},
                        {"workerId", "w-1"},
                        {"createdAt", "2026-04-05 12:34:56"},
                        {"startedAt", "2026-04-05 12:35:00"},
                        {"completedAt", "2026-04-05 12:35:30"},
                        {"cancelledAt", "2026-04-05 12:36:00"},
                        {"leaseExpiresAt", "2026-04-05 12:40:00"}};

    auto gen = models::ImageGeneration::fromJson(j);
    EXPECT_EQ(gen.user_id, 99);
    EXPECT_EQ(gen.negative_prompt, "ugly");
    EXPECT_EQ(gen.num_steps, 15);
    EXPECT_EQ(gen.request_id, "req-1");
    EXPECT_EQ(gen.image_url, "http://example.com/img.png");
    EXPECT_EQ(gen.thumbnail_url, "http://example.com/thumb.png");
    EXPECT_EQ(gen.storage_key, "task-1.png");
    EXPECT_EQ(gen.image_bytes, "bytes");
    EXPECT_EQ(gen.error_message, "oops");
    EXPECT_DOUBLE_EQ(gen.generation_time, 12.5);
    EXPECT_EQ(gen.retry_count, 2);
    EXPECT_EQ(gen.max_retries, 3);
    EXPECT_EQ(gen.failure_code, "timeout");
    EXPECT_EQ(gen.worker_id, "w-1");
    EXPECT_EQ(utils::chrono::toDbString(gen.created_at), "2026-04-05 12:34:56");
    ASSERT_TRUE(gen.started_at.has_value());
    EXPECT_EQ(utils::chrono::toDbString(*gen.started_at), "2026-04-05 12:35:00");
    ASSERT_TRUE(gen.completed_at.has_value());
    EXPECT_EQ(utils::chrono::toDbString(*gen.completed_at), "2026-04-05 12:35:30");
    ASSERT_TRUE(gen.cancelled_at.has_value());
    EXPECT_EQ(utils::chrono::toDbString(*gen.cancelled_at), "2026-04-05 12:36:00");
    ASSERT_TRUE(gen.lease_expires_at.has_value());
    EXPECT_EQ(utils::chrono::toDbString(*gen.lease_expires_at), "2026-04-05 12:40:00");
}

TEST(ImageGeneration_FromJson, MissingOptionalFields) {
    nlohmann::json j = {{"prompt", "hello"}};
    auto gen = models::ImageGeneration::fromJson(j);
    EXPECT_EQ(gen.prompt, "hello");
    EXPECT_FALSE(gen.seed.has_value());
    EXPECT_EQ(gen.negative_prompt, "");
    EXPECT_EQ(gen.image_url, "");
}

TEST(ImageGeneration_FromJson, EmptyJson) {
    auto gen = models::ImageGeneration::fromJson(nlohmann::json::object());
    EXPECT_EQ(gen.prompt, "");
    EXPECT_EQ(gen.id, 0);
}

// ==================== toJson ====================

TEST(ImageGeneration_ToJson, BasicFields) {
    models::ImageGeneration gen;
    gen.id = 42;
    gen.user_id = 1;
    gen.request_id = "req-abc";
    gen.prompt = "test prompt";
    gen.status = models::TaskStatus::Queued;
    gen.num_steps = 20;
    gen.height = 512;
    gen.width = 768;

    auto j = gen.toJson();
    EXPECT_EQ(j.at("id"), 42);
    EXPECT_EQ(j.at("taskId"), 42);
    EXPECT_EQ(j.at("userId"), 1);
    EXPECT_EQ(j.at("requestId"), "req-abc");
    EXPECT_EQ(j.at("prompt"), "test prompt");
    EXPECT_EQ(j.at("status"), "queued");
    EXPECT_EQ(j.at("numSteps"), 20);
    EXPECT_EQ(j.at("height"), 512);
    EXPECT_EQ(j.at("width"), 768);
}

TEST(ImageGeneration_ToJson, SeedIncludedWhenPresent) {
    models::ImageGeneration gen;
    gen.seed = 99;
    auto j = gen.toJson();
    EXPECT_TRUE(j.contains("seed"));
    EXPECT_EQ(j.at("seed"), 99);
}

TEST(ImageGeneration_ToJson, SeedExcludedWhenAbsent) {
    models::ImageGeneration gen;
    gen.seed = std::nullopt;
    auto j = gen.toJson();
    EXPECT_FALSE(j.contains("seed"));
}

TEST(ImageGeneration_ToJson, OptionalTimesIncludedWhenSet) {
    models::ImageGeneration gen;
    gen.completed_at = std::chrono::system_clock::now();
    auto j = gen.toJson();
    EXPECT_TRUE(j.contains("completedAt"));
}

TEST(ImageGeneration_ToJson, OptionalTimesExcludedWhenUnset) {
    models::ImageGeneration gen;
    gen.completed_at = std::nullopt;
    gen.started_at = std::nullopt;
    gen.cancelled_at = std::nullopt;
    auto j = gen.toJson();
    EXPECT_FALSE(j.contains("completedAt"));
    EXPECT_FALSE(j.contains("startedAt"));
    EXPECT_FALSE(j.contains("cancelledAt"));
}

TEST(ImageGeneration_RoundTrip, FromJsonToJsonPreservesAllFields) {
    const auto image = makeFullImageGeneration();

    const auto copy = models::ImageGeneration::fromJson(image.toJson());

    expectEquivalent(copy, image);
}

TEST(ImageGeneration_RoundTrip, FromJsonToJsonPreservesAbsentOptionals) {
    auto image = makeFullImageGeneration();
    image.seed = std::nullopt;
    image.started_at = std::nullopt;
    image.completed_at = std::nullopt;
    image.cancelled_at = std::nullopt;
    image.lease_expires_at = std::nullopt;

    const auto copy = models::ImageGeneration::fromJson(image.toJson());

    expectEquivalent(copy, image);
}

// ==================== isTerminal ====================

TEST(ImageGeneration_IsTerminal, MatchesTaskStateMachine) {
    models::ImageGeneration gen;

    gen.status = models::TaskStatus::Success;
    EXPECT_TRUE(models::isTerminal(gen.status));

    gen.status = models::TaskStatus::Failed;
    EXPECT_TRUE(models::isTerminal(gen.status));

    gen.status = models::TaskStatus::Queued;
    EXPECT_FALSE(models::isTerminal(gen.status));

    gen.status = models::TaskStatus::Generating;
    EXPECT_FALSE(models::isTerminal(gen.status));
}
