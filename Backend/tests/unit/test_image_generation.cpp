#include <gtest/gtest.h>
#include "image_generation.h"

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
                        {"negativePrompt", "ugly"},
                        {"numSteps", 15},
                        {"requestId", "req-1"},
                        {"imageUrl", "http://example.com/img.png"},
                        {"storageKey", "task-1.png"},
                        {"errorMessage", "oops"},
                        {"generationTime", 12.5},
                        {"retryCount", 2},
                        {"maxRetries", 3},
                        {"failureCode", "timeout"},
                        {"workerId", "w-1"}};

    auto gen = models::ImageGeneration::fromJson(j);
    EXPECT_EQ(gen.negative_prompt, "ugly");
    EXPECT_EQ(gen.num_steps, 15);
    EXPECT_EQ(gen.request_id, "req-1");
    EXPECT_EQ(gen.image_url, "http://example.com/img.png");
    EXPECT_EQ(gen.storage_key, "task-1.png");
    EXPECT_EQ(gen.error_message, "oops");
    EXPECT_DOUBLE_EQ(gen.generation_time, 12.5);
    EXPECT_EQ(gen.retry_count, 2);
    EXPECT_EQ(gen.max_retries, 3);
    EXPECT_EQ(gen.failure_code, "timeout");
    EXPECT_EQ(gen.worker_id, "w-1");
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
    gen.status = "queued";
    gen.num_steps = 20;
    gen.height = 512;
    gen.width = 768;

    auto j = gen.toJson(false);
    EXPECT_EQ(j.at("id"), 42);
    EXPECT_EQ(j.at("taskId"), 42);
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
    auto j = gen.toJson(false);
    EXPECT_TRUE(j.contains("seed"));
    EXPECT_EQ(j.at("seed"), 99);
}

TEST(ImageGeneration_ToJson, SeedExcludedWhenAbsent) {
    models::ImageGeneration gen;
    gen.seed = std::nullopt;
    auto j = gen.toJson(false);
    EXPECT_FALSE(j.contains("seed"));
}

TEST(ImageGeneration_ToJson, IncludeImagePayloadTrue) {
    models::ImageGeneration gen;
    gen.image_base64 = "abc123base64";
    auto j = gen.toJson(true);
    EXPECT_TRUE(j.contains("imageBase64"));
    EXPECT_EQ(j.at("imageBase64"), "abc123base64");
}

TEST(ImageGeneration_ToJson, IncludeImagePayloadFalse) {
    models::ImageGeneration gen;
    gen.image_base64 = "abc123base64";
    auto j = gen.toJson(false);
    EXPECT_FALSE(j.contains("imageBase64"));
}

TEST(ImageGeneration_ToJson, EmptyBase64NotIncludedEvenWhenTrue) {
    models::ImageGeneration gen;
    gen.image_base64 = "";
    auto j = gen.toJson(true);
    EXPECT_FALSE(j.contains("imageBase64"));
}

TEST(ImageGeneration_ToJson, OptionalTimesIncludedWhenSet) {
    models::ImageGeneration gen;
    gen.completed_at = std::chrono::system_clock::now();
    auto j = gen.toJson(false);
    EXPECT_TRUE(j.contains("completedAt"));
}

TEST(ImageGeneration_ToJson, OptionalTimesExcludedWhenUnset) {
    models::ImageGeneration gen;
    gen.completed_at = std::nullopt;
    gen.started_at = std::nullopt;
    gen.cancelled_at = std::nullopt;
    auto j = gen.toJson(false);
    EXPECT_FALSE(j.contains("completedAt"));
    EXPECT_FALSE(j.contains("startedAt"));
    EXPECT_FALSE(j.contains("cancelledAt"));
}

// ==================== isTerminal ====================

TEST(ImageGeneration_IsTerminal, MatchesTaskStateMachine) {
    models::ImageGeneration gen;

    gen.status = "success";
    EXPECT_TRUE(gen.isTerminal());

    gen.status = "failed";
    EXPECT_TRUE(gen.isTerminal());

    gen.status = "queued";
    EXPECT_FALSE(gen.isTerminal());

    gen.status = "generating";
    EXPECT_FALSE(gen.isTerminal());
}
