#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/i_image_repo.h"
#include "models/i_image_storage.h"
#include "models/image_generation.h"
#include "models/task_status.h"
#include "services/i_cache_client.h"
#include "services/image_service.h"
#include "utils/chrono_utils.h"

namespace image_service_test_fakes {

struct DisableTaskEngineWorkers {
    DisableTaskEngineWorkers() {
#ifdef _WIN32
        _putenv_s("TASK_ENGINE_WORKERS", "0");
#else
        setenv("TASK_ENGINE_WORKERS", "0", 1);
#endif
    }
};

inline DisableTaskEngineWorkers disable_task_engine_workers;

struct SetexCall {
    std::string key;
    std::string value;
    std::chrono::seconds ttl;
};

class SpyCacheClient : public cache::ICacheClient {
  public:
    bool available{true};
    std::unordered_map<std::string, std::string> values;
    mutable std::unordered_map<std::string, int64_t> versions;
    std::vector<SetexCall> setex_calls;
    std::vector<std::string> del_keys;
    std::vector<std::pair<std::string, std::string>> bump_calls;
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

    bool setex(std::string_view key, std::string_view value, std::chrono::seconds ttl) override {
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

    std::optional<int64_t> bumpVersion(std::string_view ns, std::string_view id) override {
        if (!available) {
            return std::nullopt;
        }
        bump_calls.emplace_back(std::string{ns}, std::string{id});
        auto& version = versions[std::string{ns} + ":" + std::string{id}];
        return ++version;
    }

    int64_t getVersion(std::string_view ns, std::string_view id) const override {
        if (!available) {
            return 0;
        }
        const auto it = versions.find(std::string{ns} + ":" + std::string{id});
        return it == versions.end() ? 0 : it->second;
    }
};

class FakeImageRepo : public IImageRepo {
  public:
    std::map<std::pair<int64_t, int64_t>, models::ImageGeneration> images;
    std::optional<RepoError> next_error;
    int find_by_user_calls{0};
    int find_by_status_calls{0};
    int find_by_id_calls{0};
    int insert_calls{0};
    int delete_calls{0};
    int cancel_calls{0};
    int retry_calls{0};

    RepoResult<int64_t> insert(const models::ImageGeneration& generation) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }

        ++insert_calls;
        auto saved = generation;
        if (saved.id == 0) {
            saved.id = next_id_++;
        }
        images[{saved.id, saved.user_id}] = saved;
        return saved.id;
    }

    RepoResult<ImagePageResult> findByUserId(int64_t userId, int page, int size) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }

        ++find_by_user_calls;
        std::vector<models::ImageGeneration> rows;
        for (const auto& [key, image] : images) {
            if (key.second == userId) {
                rows.push_back(image);
            }
        }
        return slicePage(std::move(rows), page, size);
    }

    RepoResult<ImagePageResult> findByUserIdAndStatus(int64_t userId, models::TaskStatus status,
                                                      int page, int size) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }

        ++find_by_status_calls;
        std::vector<models::ImageGeneration> rows;
        for (const auto& [key, image] : images) {
            if (key.second == userId && image.status == status) {
                rows.push_back(image);
            }
        }
        return slicePage(std::move(rows), page, size);
    }

    RepoResult<std::optional<models::ImageGeneration>> findByIdAndUserId(int64_t id,
                                                                         int64_t userId) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }

        ++find_by_id_calls;
        const auto it = images.find({id, userId});
        if (it == images.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    RepoResult<std::optional<models::ImageGeneration>>
    findByRequestIdAndUserId(const std::string& requestId, int64_t userId) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }

        for (const auto& [key, image] : images) {
            if (key.second == userId && image.request_id == requestId) {
                return image;
            }
        }
        return std::nullopt;
    }

    RepoResult<bool> deleteByIdAndUserId(int64_t id, int64_t userId) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }

        ++delete_calls;
        return images.erase({id, userId}) > 0;
    }

    RepoResult<std::optional<models::ImageGeneration>>
    cancelByIdAndUserId(int64_t id, int64_t userId) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }

        ++cancel_calls;
        auto it = images.find({id, userId});
        if (it == images.end()) {
            return std::nullopt;
        }
        it->second.status = models::TaskStatus::Cancelled;
        it->second.cancelled_at = stableTime();
        return it->second;
    }

    RepoResult<std::optional<models::ImageGeneration>> retryByIdAndUserId(int64_t id,
                                                                          int64_t userId) override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }

        ++retry_calls;
        auto it = images.find({id, userId});
        if (it == images.end()) {
            return std::nullopt;
        }
        it->second.status = models::TaskStatus::Queued;
        ++it->second.retry_count;
        it->second.error_message.clear();
        return it->second;
    }

    RepoResult<std::vector<ExpiredLease>> expireLeasesReturningExpired() override {
        if (auto error = consumeError()) {
            return std::unexpected(*error);
        }

        return {};
    }

    void resetListCalls() {
        find_by_user_calls = 0;
        find_by_status_calls = 0;
    }

  private:
    std::optional<RepoError> consumeError() {
        if (!next_error) {
            return std::nullopt;
        }

        auto error = std::move(next_error);
        next_error.reset();
        return error;
    }

    static std::chrono::system_clock::time_point stableTime() {
        return *utils::chrono::fromDbString("2026-01-02 03:04:05");
    }

    static ImagePageResult slicePage(std::vector<models::ImageGeneration> rows, int page,
                                     int size) {
        std::ranges::sort(rows, {}, &models::ImageGeneration::id);

        ImagePageResult result;
        result.total_elements = static_cast<int64_t>(rows.size());

        const auto safePage = (std::max)(0, page);
        const auto safeSize = (std::max)(0, size);
        const auto offset = static_cast<size_t>(safePage) * static_cast<size_t>(safeSize);
        const auto pageSize = static_cast<size_t>(safeSize);
        if (offset >= rows.size() || pageSize == 0) {
            return result;
        }

        const auto end = (std::min)(rows.size(), offset + pageSize);
        result.content.assign(rows.begin() + static_cast<std::ptrdiff_t>(offset),
                              rows.begin() + static_cast<std::ptrdiff_t>(end));
        return result;
    }

    int64_t next_id_{1000};
};

class FakeImageStorage : public IImageStorage {
  public:
    mutable std::vector<std::string> presigned_keys;
    mutable std::vector<std::string> removed_keys;
    bool return_empty_presigned_url{false};

    std::optional<std::string> getBytes(const std::string&) const override {
        return std::nullopt;
    }

    std::string presignUrl(const std::string& storageKey, int) const override {
        presigned_keys.push_back(storageKey);
        if (return_empty_presigned_url) {
            return {};
        }
        return "signed://" + storageKey;
    }

    bool remove(const std::string& storageKey) const override {
        removed_keys.push_back(storageKey);
        return true;
    }

    std::string contentTypeForKey(std::string_view) const override {
        return "image/png";
    }
};

inline models::ImageGeneration makeImage(int64_t id, int64_t userId, models::TaskStatus status) {
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

inline ImageService makeService(const std::shared_ptr<FakeImageRepo>& repo,
                                const std::shared_ptr<FakeImageStorage>& storage,
                                const std::shared_ptr<SpyCacheClient>& cache) {
    return ImageService(repo, storage, cache);
}

inline bool containsKey(const std::vector<std::string>& keys, const std::string& key) {
    return std::ranges::find(keys, key) != keys.end();
}

inline nlohmann::json cachedJsonFor(const SpyCacheClient& cache, const std::string& key) {
    const auto it = cache.values.find(key);
    if (it == cache.values.end()) {
        return {};
    }
    return nlohmann::json::parse(it->second);
}

} // namespace image_service_test_fakes
