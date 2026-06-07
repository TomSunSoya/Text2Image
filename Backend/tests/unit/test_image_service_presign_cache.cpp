#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "image_service_test_fakes.h"
#include "models/task_status.h"
#include "services/image_cache_key.h"
#include "services/image_service.h"

using image_service_test_fakes::containsKey;
using image_service_test_fakes::FakeImageRepo;
using image_service_test_fakes::FakeImageStorage;
using image_service_test_fakes::makeImage;
using image_service_test_fakes::makeService;
using image_service_test_fakes::SetexCall;
using image_service_test_fakes::SpyCacheClient;

namespace {

class ImageServicePresignCacheTest : public ::testing::Test {
  protected:
    void TearDown() override {
        ImageService::setPresignTtl(std::chrono::seconds{0});
    }
};

const SetexCall* findSetexCallForKey(const SpyCacheClient& cache, std::string_view key) {
    const auto it =
        std::ranges::find_if(cache.setex_calls, [key](const auto& call) { return call.key == key; });
    return it == cache.setex_calls.end() ? nullptr : &*it;
}

bool hasPresignSetex(const SpyCacheClient& cache) {
    return std::ranges::any_of(cache.setex_calls,
                               [](const auto& call) { return call.key.starts_with("img:url:"); });
}

} // namespace

TEST(ImageServicePresignCache, DerivePresignTtlComputes80PercentOfMinioExpiry) {
    EXPECT_EQ(image_cache::derivePresignTtl(std::chrono::seconds{3600}),
              std::chrono::seconds{2880});
    EXPECT_EQ(image_cache::derivePresignTtl(std::chrono::seconds{10}), std::chrono::seconds{8});
    EXPECT_EQ(image_cache::derivePresignTtl(std::chrono::seconds{1}), std::chrono::seconds{1});
    EXPECT_EQ(image_cache::derivePresignTtl(std::chrono::seconds{0}), std::chrono::seconds{0});
}

TEST(ImageServicePresignCache, PresignKeyIncludesStorageKey) {
    EXPECT_EQ(image_cache::presignKey("images/7/42.png"), "img:url:images/7/42.png");
}

TEST_F(ImageServicePresignCacheTest, PresignHitReturnsCachedUrl) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);
    ImageService::setPresignTtl(std::chrono::seconds{2880});

    const auto first = service.getById(7, 42, true);
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(storage->presigned_keys.size(), 1);

    storage->presigned_keys.clear();
    const auto second = service.getById(7, 42, true);

    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(storage->presigned_keys.size(), 0);
    EXPECT_EQ(second->generation.image_url, first->generation.image_url);
}

TEST_F(ImageServicePresignCacheTest, PresignMissCallsStorageAndCaches) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);
    ImageService::setPresignTtl(std::chrono::seconds{2880});

    const auto result = service.getById(7, 42, true);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(storage->presigned_keys, std::vector<std::string>{"images/7/42.png"});
    const auto key = image_cache::presignKey("images/7/42.png");
    const auto* call = findSetexCallForKey(*cache, key);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->value, result->generation.image_url);
    EXPECT_EQ(call->ttl, std::chrono::seconds{2880});
}

TEST_F(ImageServicePresignCacheTest, PresignDifferentKeysAreCachedSeparately) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    repo->images[{43, 7}] = makeImage(43, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);
    ImageService::setPresignTtl(std::chrono::seconds{2880});

    ASSERT_TRUE(service.getById(7, 42, true).has_value());
    ASSERT_TRUE(service.getById(7, 43, true).has_value());

    EXPECT_NE(findSetexCallForKey(*cache, image_cache::presignKey("images/7/42.png")), nullptr);
    EXPECT_NE(findSetexCallForKey(*cache, image_cache::presignKey("images/7/43.png")), nullptr);
    EXPECT_NE(cache->values.at(image_cache::presignKey("images/7/42.png")),
              cache->values.at(image_cache::presignKey("images/7/43.png")));
}

TEST_F(ImageServicePresignCacheTest, DeleteByIdInvalidatesPresignCache) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);
    ImageService::setPresignTtl(std::chrono::seconds{2880});

    ASSERT_TRUE(service.getById(7, 42, true).has_value());
    const auto result = service.deleteById(7, 42);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(containsKey(cache->del_keys, image_cache::presignKey("images/7/42.png")));
}

TEST_F(ImageServicePresignCacheTest, ListMyHitPresignsAllItemsViaCache) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{1, 7}] = makeImage(1, 7, models::TaskStatus::Success);
    repo->images[{2, 7}] = makeImage(2, 7, models::TaskStatus::Success);
    repo->images[{3, 7}] = makeImage(3, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);
    ImageService::setPresignTtl(std::chrono::seconds{2880});

    const auto first = service.listMy(7, 0, 10);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(storage->presigned_keys.size(), 3);

    repo->resetListCalls();
    storage->presigned_keys.clear();
    const auto second = service.listMy(7, 0, 10);

    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(second->content.size(), 3);
    EXPECT_EQ(repo->find_by_user_calls, 0);
    EXPECT_TRUE(storage->presigned_keys.empty());
    for (const auto& image : second->content) {
        EXPECT_TRUE(image.image_url.starts_with("signed://images/7/"));
    }
}

TEST_F(ImageServicePresignCacheTest, EmptyStorageKeySkipsCacheAndStorage) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    auto image = makeImage(42, 7, models::TaskStatus::Success);
    image.storage_key.clear();
    image.image_url.clear();
    repo->images[{42, 7}] = image;
    auto service = makeService(repo, storage, cache);
    ImageService::setPresignTtl(std::chrono::seconds{2880});

    const auto result = service.getById(7, 42, true);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->generation.image_url.empty());
    EXPECT_TRUE(storage->presigned_keys.empty());
    EXPECT_TRUE(std::ranges::none_of(cache->get_keys,
                                     [](const auto& key) { return key.starts_with("img:url:"); }));
}

TEST_F(ImageServicePresignCacheTest, EmptyPresignedUrlIsNotCached) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    storage->return_empty_presigned_url = true;
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);
    ImageService::setPresignTtl(std::chrono::seconds{2880});

    const auto first = service.getById(7, 42, true);
    const auto second = service.getById(7, 42, true);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(first->generation.image_url.empty());
    EXPECT_TRUE(second->generation.image_url.empty());
    EXPECT_EQ(storage->presigned_keys.size(), 2);
    EXPECT_FALSE(hasPresignSetex(*cache));
}

TEST_F(ImageServicePresignCacheTest, CacheUnavailableFallsBackToStorage) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    cache->available = false;
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);
    ImageService::setPresignTtl(std::chrono::seconds{2880});

    ASSERT_TRUE(service.getById(7, 42, true).has_value());
    ASSERT_TRUE(service.getById(7, 42, true).has_value());

    EXPECT_EQ(storage->presigned_keys.size(), 2);
    EXPECT_TRUE(cache->setex_calls.empty());
}

TEST_F(ImageServicePresignCacheTest, PresignTtlZeroDisablesCacheCompletely) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);
    ImageService::setPresignTtl(std::chrono::seconds{0});

    ASSERT_TRUE(service.getById(7, 42, true).has_value());
    ASSERT_TRUE(service.getById(7, 42, true).has_value());

    EXPECT_TRUE(std::ranges::none_of(cache->get_keys,
                                     [](const auto& key) { return key.starts_with("img:url:"); }));
    EXPECT_TRUE(std::ranges::none_of(cache->setex_calls, [](const auto& call) {
        return call.key.starts_with("img:url:");
    }));
    EXPECT_EQ(storage->presigned_keys.size(), 2);
}
