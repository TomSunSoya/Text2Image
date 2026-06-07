#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "image_service_test_fakes.h"
#include "models/task_status.h"
#include "services/image_cache_key.h"
#include "services/image_service.h"

using image_service_test_fakes::cachedJsonFor;
using image_service_test_fakes::containsKey;
using image_service_test_fakes::FakeImageRepo;
using image_service_test_fakes::FakeImageStorage;
using image_service_test_fakes::makeImage;
using image_service_test_fakes::makeService;
using image_service_test_fakes::SpyCacheClient;

TEST(ImageServiceListCache, HitReturnsCachedListWithoutRepoLookup) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{1, 7}] = makeImage(1, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    ASSERT_TRUE(service.listMy(7, 0, 10).has_value());
    repo->resetListCalls();
    storage->presigned_keys.clear();

    const auto result = service.listMy(7, 0, 10);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->content.size(), 1);
    EXPECT_EQ(result->content[0].image_url, "signed://images/7/1.png");
    EXPECT_EQ(repo->find_by_user_calls, 0);
    EXPECT_EQ(storage->presigned_keys, std::vector<std::string>{"images/7/1.png"});
}

TEST(ImageServiceListCache, MissPopulatesCacheWithSanitizedItems) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{1, 7}] = makeImage(1, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    const auto result = service.listMy(7, 0, 10);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(cache->setex_calls.size(), 1);
    const auto cached = nlohmann::json::parse(cache->setex_calls[0].value);
    ASSERT_EQ(cached.at("content").size(), 1);
    EXPECT_EQ(cached.at("content")[0].value("imageBytes", "missing"), "");
    EXPECT_EQ(cached.at("content")[0].value("imageUrl", "missing"), "");
}

TEST(ImageServiceListCache, EmptyResultIsCachedNotMarkedAsNull) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    auto service = makeService(repo, storage, cache);
    const auto key = image_cache::listMyKey(7, 0, 0, 10);

    ASSERT_TRUE(service.listMy(7, 0, 10).has_value());
    ASSERT_EQ(cache->setex_calls.size(), 1);
    EXPECT_NE(cache->setex_calls[0].value, image_cache::kNullMarker);
    EXPECT_GE(cache->setex_calls[0].ttl, image_cache::ttl::kListBase);
    EXPECT_LE(cache->setex_calls[0].ttl,
              image_cache::ttl::kListBase + image_cache::ttl::kListJitterMax);
    const auto cached = cachedJsonFor(*cache, key);
    EXPECT_EQ(cached.value("total_elements", -1), 0);
    EXPECT_TRUE(cached.at("content").empty());

    repo->resetListCalls();
    const auto result = service.listMy(7, 0, 10);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->content.empty());
    EXPECT_EQ(repo->find_by_user_calls, 0);
}

TEST(ImageServiceListCache, ListMyByStatusHitReturnsCachedListWithoutRepoLookup) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{1, 7}] = makeImage(1, 7, models::TaskStatus::Success);
    repo->images[{2, 7}] = makeImage(2, 7, models::TaskStatus::Queued);
    auto service = makeService(repo, storage, cache);

    ASSERT_TRUE(service.listMyByStatus(7, "success", 0, 10).has_value());
    repo->resetListCalls();
    storage->presigned_keys.clear();

    const auto result = service.listMyByStatus(7, "success", 0, 10);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->content.size(), 1);
    EXPECT_EQ(result->content[0].id, 1);
    EXPECT_EQ(repo->find_by_status_calls, 0);
    EXPECT_EQ(storage->presigned_keys, std::vector<std::string>{"images/7/1.png"});
}

TEST(ImageServiceListCache, PaginationKeysAreSeparate) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    for (int64_t id = 1; id <= 11; ++id) {
        repo->images[{id, 7}] = makeImage(id, 7, models::TaskStatus::Success);
    }
    auto service = makeService(repo, storage, cache);
    const auto pageZeroKey = image_cache::listMyKey(7, 0, 0, 10);
    const auto pageOneKey = image_cache::listMyKey(7, 0, 1, 10);

    ASSERT_TRUE(service.listMy(7, 0, 10).has_value());
    repo->resetListCalls();
    ASSERT_TRUE(service.listMy(7, 1, 10).has_value());

    EXPECT_EQ(repo->find_by_user_calls, 1);
    ASSERT_EQ(cache->setex_calls.size(), 2);
    EXPECT_EQ(cache->setex_calls[0].key, pageZeroKey);
    EXPECT_EQ(cache->setex_calls[1].key, pageOneKey);

    repo->resetListCalls();
    ASSERT_TRUE(service.listMy(7, 0, 10).has_value());
    ASSERT_TRUE(service.listMy(7, 1, 10).has_value());
    EXPECT_EQ(repo->find_by_user_calls, 0);
}

TEST(ImageServiceListCache, NormalizesNegativePageAndZeroSize) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{1, 7}] = makeImage(1, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);
    const auto normalizedKey = image_cache::listMyKey(7, 0, 0, 10);

    ASSERT_TRUE(service.listMy(7, -1, 0).has_value());
    EXPECT_TRUE(cache->values.contains(normalizedKey));

    repo->resetListCalls();
    ASSERT_TRUE(service.listMy(7, 0, 10).has_value());

    EXPECT_EQ(repo->find_by_user_calls, 0);
    EXPECT_EQ(cache->setex_calls.size(), 1);
}

TEST(ImageServiceListCache, CacheKeysAreIsolatedAcrossUsers) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{1, 1}] = makeImage(1, 1, models::TaskStatus::Success);
    repo->images[{1, 2}] = makeImage(1, 2, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    ASSERT_TRUE(service.listMy(1, 0, 10).has_value());
    ASSERT_TRUE(service.listMy(2, 0, 10).has_value());
    repo->resetListCalls();

    const auto first = service.listMy(1, 0, 10);
    const auto second = service.listMy(2, 0, 10);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(first->content.size(), 1);
    ASSERT_EQ(second->content.size(), 1);
    EXPECT_EQ(first->content[0].prompt, "prompt 1:1");
    EXPECT_EQ(second->content[0].prompt, "prompt 2:1");
    EXPECT_EQ(repo->find_by_user_calls, 0);
}

TEST(ImageServiceListCache, BumpVersionInvalidatesAllPagesAndStatusViews) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{1, 7}] = makeImage(1, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    ASSERT_TRUE(service.listMy(7, 0, 10).has_value());
    ASSERT_TRUE(service.listMyByStatus(7, "success", 0, 10).has_value());
    ASSERT_EQ(cache->setex_calls.size(), 2);
    repo->resetListCalls();

    const auto deleted = service.deleteById(7, 1);
    ASSERT_TRUE(deleted.has_value());

    const std::string expectedNs{image_cache::kListVersionNamespace};
    const auto listBumpsForUser = std::ranges::count_if(
        cache->bump_calls, [&](const auto& call) {
            return call.first == expectedNs && call.second == "7";
        });
    EXPECT_EQ(listBumpsForUser, 1);

    ASSERT_TRUE(service.listMy(7, 0, 10).has_value());
    ASSERT_TRUE(service.listMyByStatus(7, "success", 0, 10).has_value());

    EXPECT_EQ(repo->find_by_user_calls, 1);
    EXPECT_EQ(repo->find_by_status_calls, 1);
    ASSERT_EQ(cache->setex_calls.size(), 4);
    EXPECT_TRUE(cache->values.contains(image_cache::listMyKey(7, 1, 0, 10)));
    EXPECT_TRUE(cache->values.contains(
        image_cache::listMyStatusKey(7, 1, models::TaskStatus::Success, 0, 10)));
}

TEST(ImageServiceListCache, PerUserVersionIsIsolated) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    repo->images[{1, 1}] = makeImage(1, 1, models::TaskStatus::Success);
    repo->images[{2, 2}] = makeImage(2, 2, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    ASSERT_TRUE(service.listMy(1, 0, 10).has_value());
    ASSERT_TRUE(service.listMy(2, 0, 10).has_value());
    repo->resetListCalls();

    ASSERT_TRUE(service.deleteById(1, 1).has_value());
    ASSERT_TRUE(service.listMy(1, 0, 10).has_value());
    const auto userTwo = service.listMy(2, 0, 10);

    ASSERT_TRUE(userTwo.has_value());
    ASSERT_EQ(userTwo->content.size(), 1);
    EXPECT_EQ(userTwo->content[0].id, 2);
    EXPECT_EQ(repo->find_by_user_calls, 1);
}

TEST(ImageServiceListCache, TtlIsInJitterRange) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    auto service = makeService(repo, storage, cache);

    for (int page = 0; page < 20; ++page) {
        ASSERT_TRUE(service.listMy(7, page, 10).has_value());
    }

    ASSERT_EQ(cache->setex_calls.size(), 20);
    for (const auto& call : cache->setex_calls) {
        EXPECT_GE(call.ttl, image_cache::ttl::kListBase);
        EXPECT_LE(call.ttl, image_cache::ttl::kListBase + image_cache::ttl::kListJitterMax);
    }
}

TEST(ImageServiceListCache, CacheUnavailableFallsBackToRepo) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    cache->available = false;
    repo->images[{1, 7}] = makeImage(1, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    ASSERT_TRUE(service.listMy(7, 0, 10).has_value());
    ASSERT_TRUE(service.listMy(7, 0, 10).has_value());

    EXPECT_EQ(repo->find_by_user_calls, 2);
    EXPECT_TRUE(cache->setex_calls.empty());
}

TEST(ImageServiceListCache, DirtyCachedJsonIsEvictedAndFallsBackToRepo) {
    auto repo = std::make_shared<FakeImageRepo>();
    auto storage = std::make_shared<FakeImageStorage>();
    auto cache = std::make_shared<SpyCacheClient>();
    const auto key = image_cache::listMyKey(7, 0, 0, 10);
    cache->values[key] = "{not-json";
    repo->images[{1, 7}] = makeImage(1, 7, models::TaskStatus::Success);
    auto service = makeService(repo, storage, cache);

    const auto result = service.listMy(7, 0, 10);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(repo->find_by_user_calls, 1);
    EXPECT_TRUE(containsKey(cache->del_keys, key));
    ASSERT_EQ(cache->setex_calls.size(), 1);
    EXPECT_EQ(cache->setex_calls[0].key, key);
}
