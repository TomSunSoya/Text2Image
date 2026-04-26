#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "database/i_image_repo.h"
#include "models/i_image_storage.h"
#include "services/i_cache_client.h"
#include "services/image_cache_key.h"
#include "services/image_service.h"
#include "utils/chrono_utils.h"

namespace {

struct DisableTaskEngineWorkersForUnitTests {
    DisableTaskEngineWorkersForUnitTests() {
#ifdef _WIN32
        _putenv_s("TASK_ENGINE_WORKERS", "0");
#else
        setenv("TASK_ENGINE_WORKERS", "0", 1);
#endif
    }
};

DisableTaskEngineWorkersForUnitTests disableTaskEngineWorkersForUnitTests;

struct SetexCall {
    std::string key;
    std::string value;
    std::chrono::seconds ttl;
};

class SpyCacheClient : public cache::ICacheClient {
  public:
    bool available{true};
    std::unordered_map<std::string, std::string> values;
    std::vector<SetexCall> setex_calls;
    std::vector<std::string> del_keys;
    mutable std::vector<std::string> get_keys;

    [[nodiscard]] bool isAvailable() const noexcept override {
        return available;
    }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const override {
        get_keys.emplace_back(key);
        if (!available) {
            return std::nullopt;
        }

        const auto it = values.find(std::string{key});
        if (it == values.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool setex(std::string_view key, std::string_view value,
               std::chrono::seconds ttl) override {
        if (!available) {
            return false;
        }

        setex_calls.push_back(SetexCall{std::string{key}, std::string{value}, ttl});
        values[std::string{key}] = std::string{value};
        return true;
    }

    bool del(std::string_view key) override {
        del_keys.emplace_back(key);
        if (!available) {
            return false;
        }

        return values.erase(std::string{key}) > 0;
    }

    std::optional<int64_t> bumpVersion(std::string_view, std::string_view) override {
        return std::nullopt;
    }

    int64_t getVersion(std::string_view, std::string_view) const override {
        return 0;
    }
};

class FakeImageRepo : public IImageRepo {
  public:
    std::map<std::pair<int64_t, int64_t>, models::ImageGeneration> images;
    int find_by_id_calls{0};
    int cancel_calls{0};
    int retry_calls{0};
    int delete_calls{0};

    int64_t insert(const models::ImageGeneration& generation) override {
        auto saved = generation;
        if (saved.id == 0) {
            saved.id = next_id_++;
        }
        images[{saved.id, saved.user_id}] = saved;
        return saved.id;
    }

    ImagePageResult findByUserId(int64_t, int, int) override {
        return {};
    }

    ImagePageResult findByUserIdAndStatus(int64_t, models::TaskStatus, int, int) override {
        return {};
    }

    std::optional<models::ImageGeneration> findByIdAndUserId(int64_t id,
                                                             int64_t userId) override {
        ++find_by_id_calls;
        const auto it = images.find({id, userId});
        if (it == images.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<models::ImageGeneration> findByRequestIdAndUserId(const std::string&,
                                                                    int64_t) override {
        return std::nullopt;
    }

    bool deleteByIdAndUserId(int64_t id, int64_t userId) override {
        ++delete_calls;
        return images.erase({id, userId}) > 0;
    }

    bool cancelByIdAndUserId(int64_t id, int64_t userId,
                             models::ImageGeneration* updated) override {
        ++cancel_calls;
        auto it = images.find({id, userId});
        if (it == images.end()) {
            return false;
        }

        it->second.status = models::TaskStatus::Cancelled;
        it->second.cancelled_at = stableTime();
        if (updated != nullptr) {
            *updated = it->second;
        }
        return true;
    }

    bool retryByIdAndUserId(int64_t id, int64_t userId,
                            models::ImageGeneration* updated) override {
        ++retry_calls;
        auto it = images.find({id, userId});
        if (it == images.end()) {
            return false;
        }

        it->second.status = models::TaskStatus::Queued;
        ++it->second.retry_count;
        it->second.error_message.clear();
        if (updated != nullptr) {
            *updated = it->second;
        }
        return true;
    }

    std::vector<ExpiredLease> expireLeasesReturningExpired() override {
        return {};
    }

  private:
    static std::chrono::system_clock::time_point stableTime() {
        return *utils::chrono::fromDbString("2026-01-02 03:04:05");
    }

    int64_t next_id_{1000};
};

class FakeImageStorage : public IImageStorage {
  public:
    mutable std::vector<std::string> presigned_keys;
    mutable std::vector<std::string> removed_keys;

    std::optional<std::string> getBytes(const std::string&) const override {
        return std::nullopt;
    }

    std::string presignUrl(const std::string& storageKey, int) const override {
        presigned_keys.push_back(storageKey);
        return "signed://" + storageKey;
    }

    bool remove(const std::string& storageKey) const override {
        removed_keys.push_back(storageKey);
        return true;
    }

    std::string contentTypeForKey(const std::string&) const override {
        return "image/png";
    }
};

models::ImageGeneration makeImage(int64_t id, int64_t userId, models::TaskStatus status) {
    models::ImageGeneration image;
    image.id = id;
    image.user_id = userId;
    image.request_id = "req-" + std::to_string(userId) + "-" + std::to_string(id);
    image.prompt = "prompt " + std::to_string(userId) + ":" + std::to_string(id);
    image.negative_prompt = "low quality";
    image.num_steps = 8;
    image.height = 768;
    image.width = 768;
    image.seed = 42;
    image.status = status;
    image.retry_count = 0;
    image.max_retries = 3;
    image.failure_code = "";
    image.worker_id = "worker-1";
    image.image_url = "https://original.example/image.png";
    image.thumbnail_url = "https://original.example/thumb.png";
    image.storage_key = "images/" + std::to_string(userId) + "/" + std::to_string(id) + ".png";
    image.image_bytes = "raw-bytes";
    image.error_message = "";
    image.generation_time = 1.25;
    image.created_at = *utils::chrono::fromDbString("2026-01-02 03:04:05");
    return image;
}

ImageService makeService(const std::shared_ptr<FakeImageRepo>& repo,
                         const std::shared_ptr<FakeImageStorage>& storage,
                         const std::shared_ptr<SpyCacheClient>& cache) {
    return ImageService(repo, storage, cache);
}

bool containsKey(const std::vector<std::string>& keys, const std::string& key) {
    return std::ranges::find(keys, key) != keys.end();
}

} // namespace

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
