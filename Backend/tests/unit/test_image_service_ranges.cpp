#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "image_service_test_fakes.h"
#include "models/task_status.h"
#include "services/image_cache_key.h"
#include "services/image_service.h"

using image_service_test_fakes::FakeImageRepo;
using image_service_test_fakes::FakeImageStorage;
using image_service_test_fakes::makeImage;
using image_service_test_fakes::makeService;
using image_service_test_fakes::SpyCacheClient;

TEST(ImageServiceRangesSanitization, WriteListCacheSanitizesImageBytesAndUrl) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();

    // Create an image with non-empty bytes and URL
    auto img = makeImage(1, 7, models::TaskStatus::Success);
    img.image_bytes = "raw-binary-data";
    img.image_url = "https://example.com/image.png";
    repo->images[{1, 7}] = img;

    auto service = makeService(repo, storage, cache);

    const auto result = service.listMy(7, 0, 10);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(cache->setex_calls.size(), 1);

    // Verify the cached JSON has sanitized fields
    const auto cached = nlohmann::json::parse(cache->setex_calls[0].value);
    ASSERT_TRUE(cached.contains("content"));
    ASSERT_EQ(cached["content"].size(), 1);

    // The ranges-based sanitization should clear image_bytes and image_url
    EXPECT_EQ(cached["content"][0].value("imageBytes", "not-empty"), "");
    EXPECT_EQ(cached["content"][0].value("imageUrl", "not-empty"), "");
}

TEST(ImageServiceRangesSanitization, WriteListCacheHandlesMultipleItems) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();

    // Create multiple images
    for (int i = 1; i <= 3; ++i) {
        auto img = makeImage(i, 7, models::TaskStatus::Success);
        img.image_bytes = "data-" + std::to_string(i);
        img.image_url = "url-" + std::to_string(i);
        repo->images[{i, 7}] = img;
    }

    auto service = makeService(repo, storage, cache);
    const auto result = service.listMy(7, 0, 10);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(cache->setex_calls.size(), 1);

    const auto cached = nlohmann::json::parse(cache->setex_calls[0].value);
    ASSERT_EQ(cached["content"].size(), 3);

    // All items should be sanitized
    for (const auto& item : cached["content"]) {
        EXPECT_EQ(item.value("imageBytes", "not-empty"), "");
        EXPECT_EQ(item.value("imageUrl", "not-empty"), "");
    }
}

TEST(ImageServiceRangesSanitization, WriteListCacheHandlesEmptyList) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();

    auto service = makeService(repo, storage, cache);
    const auto result = service.listMy(7, 0, 10);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(cache->setex_calls.size(), 1);

    const auto cached = nlohmann::json::parse(cache->setex_calls[0].value);
    EXPECT_TRUE(cached["content"].is_array());
    EXPECT_TRUE(cached["content"].empty());
    EXPECT_EQ(cached.value("total_elements", -1), 0);
}