#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "image_service_test_fakes.h"
#include "models/image_generation.h"
#include "services/image_cache_key.h"
#include "services/image_service.h"

using image_service_test_fakes::cachedJsonFor;
using image_service_test_fakes::containsKey;
using image_service_test_fakes::FakeImageRepo;
using image_service_test_fakes::FakeImageStorage;
using image_service_test_fakes::makeImage;
using image_service_test_fakes::makeService;
using image_service_test_fakes::SpyCacheClient;

TEST(ImageServiceCache, HitReturnsCachedImageWithoutRepoLookup) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    const auto key = image_cache::metaKey(7, 42);
    auto cached = makeImage(42, 7, models::TaskStatus::Success);
    cached.prompt = "from cache";
    cache->values[key] = cached.toJson().dump();
    auto service = makeService(repo, storage, cache);

    const auto result = service.getById(7, 42, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.prompt, "from cache");
    EXPECT_EQ(repo->find_by_id_calls, 0);
    EXPECT_EQ(cache->get_keys, std::vector<std::string>{key});
    EXPECT_TRUE(cache->setex_calls.empty());
}

TEST(ImageServiceCache, MissLoadsFromRepoAndWritesSanitizedCacheEntry) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    const auto result = service.getById(7, 42, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.prompt, "prompt 7:42");
    EXPECT_EQ(repo->find_by_id_calls, 1);
    ASSERT_EQ(cache->setex_calls.size(), 1);
    EXPECT_EQ(cache->setex_calls[0].key, image_cache::metaKey(7, 42));
    const auto cached =
        models::ImageGeneration::fromJson(nlohmann::json::parse(cache->setex_calls[0].value));
    EXPECT_EQ(cached.prompt, "prompt 7:42");
    EXPECT_TRUE(cached.image_bytes.empty());
    EXPECT_TRUE(cached.image_url.empty());
}

TEST(ImageServiceCache, RepoMissWritesNullMarkerWithShortTtl) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    auto service = makeService(repo, storage, cache);

    const auto result = service.getById(7, 404, false);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k404NotFound);
    ASSERT_EQ(cache->setex_calls.size(), 1);
    EXPECT_EQ(cache->setex_calls[0].key, image_cache::metaKey(7, 404));
    EXPECT_EQ(cache->setex_calls[0].value, image_cache::kNullMarker);
    EXPECT_EQ(cache->setex_calls[0].ttl, image_cache::ttl::kNullMarker);
}

TEST(ImageServiceCache, NullMarkerHitReturnsNotFoundWithoutRepoLookup) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    const auto key = image_cache::metaKey(7, 404);
    cache->values[key] = std::string{image_cache::kNullMarker};
    auto service = makeService(repo, storage, cache);

    const auto result = service.getById(7, 404, false);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().status, drogon::k404NotFound);
    EXPECT_EQ(repo->find_by_id_calls, 0);
    EXPECT_TRUE(cache->setex_calls.empty());
}

TEST(ImageServiceCache, CacheKeysAreIsolatedAcrossUsers) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    const auto userOneKey = image_cache::metaKey(1, 99);
    const auto userTwoKey = image_cache::metaKey(2, 99);
    auto cached = makeImage(99, 1, models::TaskStatus::Success);
    cached.prompt = "user one cached";
    cache->values[userOneKey] = cached.toJson().dump();
    auto userTwoImage = makeImage(99, 2, models::TaskStatus::Success);
    userTwoImage.prompt = "user two db";
    repo->images[{99, 2}] = userTwoImage;
    auto service = makeService(repo, storage, cache);

    const auto first = service.getById(1, 99, false);
    const auto second = service.getById(2, 99, false);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->generation.prompt, "user one cached");
    EXPECT_EQ(second->generation.prompt, "user two db");
    EXPECT_EQ(repo->find_by_id_calls, 1);
    EXPECT_EQ(cache->get_keys, (std::vector<std::string>{userOneKey, userTwoKey}));
}

TEST(ImageServiceCache, CancelEvictsMetaCache) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    const auto key = image_cache::metaKey(7, 42);
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Queued);
    cache->values[key] = "stale";
    auto service = makeService(repo, storage, cache);

    const auto result = service.cancelById(7, 42);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.status, models::TaskStatus::Cancelled);
    EXPECT_TRUE(containsKey(cache->del_keys, key));
    EXPECT_FALSE(cache->values.contains(key));
}

TEST(ImageServiceCache, RetryEvictsMetaCache) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    const auto key = image_cache::metaKey(7, 42);
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Cancelled);
    cache->values[key] = "stale";
    auto service = makeService(repo, storage, cache);

    const auto result = service.retryById(7, 42);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.status, models::TaskStatus::Queued);
    EXPECT_TRUE(containsKey(cache->del_keys, key));
    EXPECT_FALSE(cache->values.contains(key));
}

TEST(ImageServiceCache, DeleteEvictsMetaCache) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    const auto key = image_cache::metaKey(7, 42);
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    cache->values[key] = "stale";
    auto service = makeService(repo, storage, cache);

    const auto result = service.deleteById(7, 42);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(containsKey(cache->del_keys, key));
    EXPECT_FALSE(cache->values.contains(key));
    EXPECT_EQ(repo->delete_calls, 1);
    EXPECT_EQ(storage->removed_keys, std::vector<std::string>{"images/7/42.png"});
}

TEST(ImageServiceCache, CacheWriteTtlUsesTerminalAndInflightValues) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{10, 7}] = makeImage(10, 7, models::TaskStatus::Success);
    repo->images[{11, 7}] = makeImage(11, 7, models::TaskStatus::Generating);
    auto service = makeService(repo, storage, cache);

    const auto terminal = service.getById(7, 10, false);
    const auto inflight = service.getById(7, 11, false);

    ASSERT_TRUE(terminal.has_value());
    ASSERT_TRUE(inflight.has_value());
    ASSERT_EQ(cache->setex_calls.size(), 2);
    EXPECT_EQ(cache->setex_calls[0].ttl, image_cache::ttl::kMetaTerminal);
    EXPECT_EQ(cache->setex_calls[1].ttl, image_cache::ttl::kMetaInflight);
}

TEST(ImageServiceCache, CacheUnavailableFallsBackToRepoWithoutFailingRequest) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    cache->available = false;
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    const auto result = service.getById(7, 42, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.prompt, "prompt 7:42");
    EXPECT_EQ(repo->find_by_id_calls, 1);
    EXPECT_EQ(cache->get_keys, std::vector<std::string>{image_cache::metaKey(7, 42)});
    EXPECT_TRUE(cache->setex_calls.empty());
}

TEST(ImageServiceCache, DirtyCachedJsonIsEvictedAndFallsBackToRepo) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    const auto key = image_cache::metaKey(7, 42);
    cache->values[key] = "{not-json";
    repo->images[{42, 7}] = makeImage(42, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    const auto result = service.getById(7, 42, false);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->generation.prompt, "prompt 7:42");
    EXPECT_EQ(repo->find_by_id_calls, 1);
    EXPECT_TRUE(containsKey(cache->del_keys, key));
    ASSERT_EQ(cache->setex_calls.size(), 1);
    EXPECT_EQ(cache->setex_calls[0].key, key);
}
