#include "services/image_service.h"

#include <cctype>
#include <algorithm>
#include <chrono>
#include <format>
#include <iterator>
#include <optional>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

#include "database/ImageRepo.h"
#include "models/image_storage.h"
#include "models/task_status.h"
#include "services/generation_client.h"
#include "services/image_cache_key.h"
#include "services/metrics_registry.h"
#include "services/null_cache_client.h"
#include "services/rate_limiter.h"
#include "services/redis_client.h"
#include "services/repo_error_mapper.h"
#include "services/task_engine.h"
#include "services/task_event_hub.h"

namespace {

TaskEngine& taskEngine() {
    static TaskEngine engine;
    return engine;
}

std::shared_ptr<cache::ICacheClient>& defaultCacheClient() {
    static std::shared_ptr<cache::ICacheClient> client = std::make_shared<cache::NullCacheClient>();
    return client;
}

std::string buildRequestId() {
    thread_local std::mt19937_64 rng(std::random_device{}());
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const auto rand = std::uniform_int_distribution<uint64_t>{}(rng);
    return std::format("img-{}-{:x}", millis, rand);
}

constexpr size_t kPromptMinLength = 3;
constexpr size_t kPromptMaxLength = 1000;
constexpr size_t kNegativePromptMaxLength = 500;
constexpr int kMinImageSize = 512;
constexpr int kMaxImageSize = 2048;
constexpr int kImageSizeStep = 64;
constexpr int kMinNumSteps = 1;
constexpr int kMaxNumSteps = 50;

void trimInPlace(std::string& s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::ranges::find_if(s, notSpace));
    s.erase(std::ranges::find_if(s | std::views::reverse, notSpace).base(), s.end());
}

std::optional<ServiceError> validateGenerationParams(models::ImageGeneration& generation) {
    trimInPlace(generation.prompt);
    trimInPlace(generation.negative_prompt);

    if (generation.prompt.empty()) {
        return ServiceError{drogon::k400BadRequest, "prompt_required", "prompt is required"};
    }

    if (generation.prompt.size() < kPromptMinLength ||
        generation.prompt.size() > kPromptMaxLength) {
        return ServiceError{drogon::k400BadRequest, "invalid_prompt_length",
                            std::format("prompt length must be between {} and {} characters",
                                        kPromptMinLength, kPromptMaxLength)};
    }

    if (generation.negative_prompt.size() > kNegativePromptMaxLength) {
        return ServiceError{
            drogon::k400BadRequest, "invalid_negative_prompt_length",
            std::format("negative_prompt must be at most {} characters", kNegativePromptMaxLength)};
    }

    if (generation.num_steps < kMinNumSteps || generation.num_steps > kMaxNumSteps) {
        return ServiceError{
            drogon::k400BadRequest, "invalid_num_steps",
            std::format("num_steps must be between {} and {}", kMinNumSteps, kMaxNumSteps)};
    }

    if (generation.width < kMinImageSize || generation.width > kMaxImageSize ||
        generation.width % kImageSizeStep != 0) {
        return ServiceError{drogon::k400BadRequest, "invalid_width",
                            std::format("width must be between {} and {} and a multiple of {}",
                                        kMinImageSize, kMaxImageSize, kImageSizeStep)};
    }

    if (generation.height < kMinImageSize || generation.height > kMaxImageSize ||
        generation.height % kImageSizeStep != 0) {
        return ServiceError{drogon::k400BadRequest, "invalid_height",
                            std::format("height must be between {} and {} and a multiple of {}",
                                        kMinImageSize, kMaxImageSize, kImageSizeStep)};
    }

    if (generation.seed.has_value() && generation.seed.value() < 0) {
        return ServiceError{drogon::k400BadRequest, "invalid_seed", "seed must be >= 0"};
    }

    return std::nullopt;
}

std::chrono::seconds& presignTtlRef() {
    static std::chrono::seconds ttl{0};
    return ttl;
}

int maxActiveTasksPerUser() {
    try {
        return rate_limit::loadRateLimitConfig().max_active_tasks_per_user;
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load rate limit config, using active task default: {}", ex.what());
        return rate_limit::RateLimitConfig{}.max_active_tasks_per_user;
    }
}

} // namespace

void ImageService::presignListImagesInPlace(std::vector<models::ImageGeneration>& images) const {
    for (auto& img :
         images | std::views::filter([](const auto& i) { return !i.storage_key.empty(); })) {
        try {
            img.image_url = presignWithCache(img.storage_key);
        } catch (const std::exception& ex) {
            spdlog::error("presignListImages: failed to presign storage_key='{}': {}",
                          img.storage_key, ex.what());
        } catch (...) {
            spdlog::error("presignListImages: unknown error for storage_key='{}'", img.storage_key);
        }
    }
}

void ImageService::writeToCache(std::string_view key, const models::ImageGeneration& image) const {
    try {
        auto sanitized = image;
        sanitized.image_bytes.clear();
        sanitized.image_url.clear();
        const auto ttl = models::isTerminal(image.status) ? image_cache::ttl::kMetaTerminal
                                                          : image_cache::ttl::kMetaInflight;
        cache_->setex(key, sanitized.toJson().dump(), ttl);
    } catch (const std::exception& ex) {
        spdlog::warn("ImageService::writeToCache failed for key '{}': {}", key, ex.what());
    }
}

void ImageService::presignInPlace(models::ImageGeneration& image) const {
    try {
        image.image_url = presignWithCache(image.storage_key);
    } catch (const std::exception& ex) {
        spdlog::warn("ImageService::presignInPlace failed to generate presigned URL for id={}, "
                     "user_id={}, reason={}",
                     image.id, image.user_id, ex.what());
    }
}

void ImageService::writeListCache(std::string_view key, const ImageListResult& result) const {
    try {
        nlohmann::json j;
        j["total_elements"] = result.total_elements;
        std::vector<nlohmann::json> content;
        content.reserve(result.content.size());
        std::ranges::transform(result.content, std::back_inserter(content), [](const auto& img) {
            auto sanitized = img;
            sanitized.image_bytes.clear();
            sanitized.image_url.clear();
            return sanitized.toJson();
        });
        j["content"] = nlohmann::json(std::move(content));
        cache_->setex(key, j.dump(), image_cache::listTtlWithJitter());
    } catch (const std::exception& ex) {
        spdlog::warn("ImageService::writeListCache failed for key '{}': {}", key, ex.what());
    }
}

void ImageService::invalidateListCacheFor(int64_t userId) const {
    cache_->bumpVersion(image_cache::kListVersionNamespace, std::to_string(userId));
}

std::string ImageService::presignWithCache(const std::string& storageKey) const {
    if (storageKey.empty()) {
        return {};
    }

    const auto ttl = presignTtlRef();
    if (ttl <= std::chrono::seconds{0}) {
        // no caching, directly presign from storage
        return storage_->presignUrl(storageKey);
    }

    const auto key = image_cache::presignKey(storageKey);
    if (auto cached = cache_->get(key)) {
        return std::move(*cached);
    }

    // not in cache, presign and write to cache
    auto url = storage_->presignUrl(storageKey);
    if (!url.empty())
        cache_->setex(key, url, ttl);
    return url;
}

ImageService::ImageService()
    : repo_(std::make_shared<ImageRepo>()), storage_(std::make_shared<ImageStorage>()),
      cache_(defaultCacheClient()) {}

ImageService::ImageService(std::shared_ptr<IImageRepo> repo, std::shared_ptr<IImageStorage> storage)
    : ImageService(std::move(repo), std::move(storage), defaultCacheClient()) {}

ImageService::ImageService(std::shared_ptr<IImageRepo> repo, std::shared_ptr<IImageStorage> storage,
                           std::shared_ptr<cache::ICacheClient> cache)
    : repo_(std::move(repo)), storage_(std::move(storage)),
      cache_(cache ? std::move(cache) : defaultCacheClient()) {
    if (!repo_) {
        throw std::invalid_argument("ImageService: repo must not be null");
    }
    if (!storage_) {
        throw std::invalid_argument("ImageService: storage must not be null");
    }
}

void ImageService::bootstrapWorkers(std::shared_ptr<cache::ICacheClient> cache) {
    taskEngine().bootstrap(cache);
}

void ImageService::setDefaultCache(std::shared_ptr<cache::ICacheClient> cache) {
    defaultCacheClient() = cache ? std::move(cache) : std::make_shared<cache::NullCacheClient>();
}

void ImageService::setPresignTtl(std::chrono::seconds ttl) {
    presignTtlRef() = ttl;
}

std::expected<ImageCreateResult, ServiceError>
ImageService::create(int64_t userId, const nlohmann::json& payload, bool isAdmin) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    models::ImageGeneration generation = models::ImageGeneration::fromJson(payload);

    if (auto validationError = validateGenerationParams(generation)) {
        return std::unexpected(std::move(*validationError));
    }

    const auto maxActive = maxActiveTasksPerUser();
    if (!isAdmin && maxActive > 0) {
        auto activeTasks = repo_->countActiveTasksByUserId(userId);
        if (!activeTasks) {
            return std::unexpected(mapRepoError(activeTasks.error()));
        }
        if (*activeTasks >= maxActive) {
            ServiceError error = ServiceError::tooManyRequests(
                "too_many_active_tasks",
                std::format("too many active image tasks, limit is {}", maxActive));
            error.details["maxActiveTasks"] = maxActive;
            error.details["activeTasks"] = *activeTasks;
            return std::unexpected(std::move(error));
        }
    }

    generation.user_id = userId;
    generation.created_at = std::chrono::system_clock::now();
    generation.request_id =
        generation.request_id.empty() ? buildRequestId() : generation.request_id;
    generation.status = models::TaskStatus::Queued;
    generation.error_message.clear();
    generation.completed_at = std::nullopt;
    generation.image_url.clear();
    generation.image_bytes.clear();
    generation.generation_time = 0;

    auto existing = repo_->findByRequestIdAndUserId(generation.request_id, userId);
    if (!existing) {
        return std::unexpected(mapRepoError(existing.error()));
    }

    if (*existing) {
        spdlog::info("ImageService create with existing request_id, returning existing "
                     "generation, request_id={}, user_id={}",
                     generation.request_id, userId);
        return ImageCreateResult{**existing};
    }

    auto insertedId = repo_->insert(generation);
    if (!insertedId) {
        return std::unexpected(mapRepoError(insertedId.error()));
    }

    generation.id = *insertedId;
    metrics::MetricsRegistry::instance().recordTaskStatus(generation.status);

    try {
        TaskEventHub::instance().publishTaskUpdated(generation);
        writeToCache(image_cache::metaKey(userId, generation.id), generation);
        invalidateListCacheFor(userId);

        taskEngine().enqueue(generation.id);
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::create enqueue error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError,
                                            "image_task_enqueue_failed",
                                            "failed to enqueue image task"});
    }

    return ImageCreateResult{generation};
}

std::expected<ImageListResult, ServiceError> ImageService::listMy(int64_t userId, int page,
                                                                  int size) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    const auto normalizedPage = image_cache::normalizePage(page);
    const auto normalizedSize = image_cache::normalizeSize(size);

    const auto userIdStr = std::to_string(userId);
    const auto version = cache_->getVersion(image_cache::kListVersionNamespace, userIdStr);
    const auto key = image_cache::listMyKey(userId, version, normalizedPage, normalizedSize);

    // read from cache first
    if (auto cached = cache_->get(key)) {
        try {
            auto parsed = nlohmann::json::parse(*cached);
            ImageListResult result;
            result.total_elements = parsed.value("total_elements", (int64_t)0);
            for (const auto& item : parsed.value("content", nlohmann::json::array())) {
                result.content.push_back(models::ImageGeneration::fromJson(item));
            }

            // presign URLs in place
            presignListImagesInPlace(result.content);
            return result;
        } catch (const std::exception& ex) {
            spdlog::warn("ImageService::listMy failed to parse cached value, key={}, reason={}; "
                         "evicting and "
                         "falling back to DB",
                         key, ex.what());
            cache_->del(key);
            // no record in cache, fall through to load from DB
        }
    }

    auto repoPage = repo_->findByUserId(userId, normalizedPage, normalizedSize);
    if (!repoPage) {
        return std::unexpected(mapRepoError(repoPage.error()));
    }

    ImageListResult result{std::move(repoPage->content), repoPage->total_elements};
    writeListCache(key, result);

    presignListImagesInPlace(result.content);
    return result;
}

std::expected<ImageListResult, ServiceError>
ImageService::listMyByStatus(int64_t userId, std::string_view status, int page, int size) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    const auto target = models::normalizeTaskStatus(status);
    const auto normalizedPage = image_cache::normalizePage(page);
    const auto normalizedSize = image_cache::normalizeSize(size);

    const auto userIdStr = std::to_string(userId);
    const auto version = cache_->getVersion(image_cache::kListVersionNamespace, userIdStr);
    const auto key =
        image_cache::listMyStatusKey(userId, version, target, normalizedPage, normalizedSize);

    if (auto cached = cache_->get(key)) {
        try {
            auto parsed = nlohmann::json::parse(*cached);
            ImageListResult result;
            result.total_elements = parsed.value("total_elements", (int64_t)0);
            for (const auto& item : parsed.value("content", nlohmann::json::array())) {
                result.content.push_back(models::ImageGeneration::fromJson(item));
            }

            presignListImagesInPlace(result.content);
            return result;
        } catch (const std::exception& ex) {
            spdlog::warn("ImageService::listMyByStatus failed to parse cached value, key={}, "
                         "reason={}; evicting and falling back to DB",
                         key, ex.what());
            cache_->del(key);
        }
    }

    auto repoPage = repo_->findByUserIdAndStatus(userId, target, normalizedPage, normalizedSize);
    if (!repoPage) {
        return std::unexpected(mapRepoError(repoPage.error()));
    }

    ImageListResult result{std::move(repoPage->content), repoPage->total_elements};
    writeListCache(key, result);

    presignListImagesInPlace(result.content);
    return result;
}

std::expected<ImageGetResult, ServiceError> ImageService::getById(int64_t userId, int64_t id,
                                                                  bool includeImagePayload) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    const auto key = image_cache::metaKey(userId, id);

    // read from cache first
    if (auto cached = cache_->get(key)) {
        if (*cached == image_cache::kNullMarker) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
        }

        try {
            auto image = models::ImageGeneration::fromJson(nlohmann::json::parse(*cached));
            if (includeImagePayload && !image.storage_key.empty()) {
                presignInPlace(image);
            }
            return ImageGetResult{std::move(image)};
        } catch (const std::exception& ex) {
            spdlog::warn("ImageService::getById failed to parse cached value, key={}, reason={}; "
                         "evicting and falling back to DB",
                         key, ex.what());
            cache_->del(key);
            // fall through to load from DB
        }
    }

    auto image = repo_->findByIdAndUserId(id, userId);
    if (!image) {
        return std::unexpected(mapRepoError(image.error()));
    }

    if (!*image) {
        cache_->setex(key, image_cache::kNullMarker,
                      image_cache::ttl::kNullMarker); // cache null result
        return std::unexpected(
            ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
    }

    auto generation = **image;
    writeToCache(key, generation);

    if (includeImagePayload && !generation.storage_key.empty()) {
        presignInPlace(generation);
    }

    return ImageGetResult{std::move(generation)};
}

std::expected<ImageGetResult, ServiceError> ImageService::cancelById(int64_t userId,
                                                                     int64_t id) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    auto current = repo_->findByIdAndUserId(id, userId);
    if (!current) {
        return std::unexpected(mapRepoError(current.error()));
    }

    if (!*current) {
        return std::unexpected(
            ServiceError{drogon::k404NotFound, "task_not_found", "task not found"});
    }

    const auto& currentImage = **current;
    if (!models::canCancel(currentImage.status)) {
        ServiceError err{drogon::k400BadRequest, "task_cancel_not_allowed",
                         "task is already completed and cannot be cancelled"};
        err.details["status"] = models::statusToStdString(currentImage.status);
        return std::unexpected(std::move(err));
    }

    auto updated = repo_->cancelByIdAndUserId(id, userId);
    if (!updated) {
        return std::unexpected(mapRepoError(updated.error()));
    }

    if (!*updated) {
        return std::unexpected(
            ServiceError{drogon::k409Conflict, "task_cancel_conflict", "task cannot be canceled"});
    }

    try {
        try {
            auto& r = redis::RedisClient::instance();
            if (r.isAvailable()) {
                (void)r.removeFromQueue(id);
                r.forceReleaseLease(id);
            }
        } catch (const std::exception& ex) {
            spdlog::warn("ImageService::cancelById Redis cleanup failed, id={}, user_id={}, "
                         "reason={}",
                         id, userId, ex.what());
        } catch (...) {
            spdlog::warn("ImageService::cancelById Redis cleanup failed, id={}, user_id={}, "
                         "reason=unknown",
                         id, userId);
        }

        TaskEventHub::instance().publishTaskUpdated(**updated);
        metrics::MetricsRegistry::instance().recordTaskStatus((*updated)->status);
        cache_->del(image_cache::metaKey(userId, id)); // evict cache
        invalidateListCacheFor(userId);
        return ImageGetResult{**updated};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::cancelById post-update error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError, "task_cancel_failed",
                                            "failed to cancel task"});
    }
}

std::expected<ImageGetResult, ServiceError> ImageService::retryById(int64_t userId,
                                                                    int64_t id) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    auto current = repo_->findByIdAndUserId(id, userId);
    if (!current) {
        return std::unexpected(mapRepoError(current.error()));
    }

    if (!*current) {
        return std::unexpected(
            ServiceError{drogon::k404NotFound, "task_not_found", "task not found"});
    }

    const auto& currentImage = **current;
    if (!models::canRetry(currentImage.status, currentImage.retry_count,
                          currentImage.max_retries)) {
        ServiceError err{drogon::k409Conflict, "task_retry_not_allowed",
                         "only failed, timeout or canceled tasks can be retried"};
        err.details["status"] = models::statusToStdString(currentImage.status);
        err.details["retryCount"] = currentImage.retry_count;
        err.details["maxRetries"] = currentImage.max_retries;
        return std::unexpected(std::move(err));
    }

    auto updated = repo_->retryByIdAndUserId(id, userId);
    if (!updated) {
        return std::unexpected(mapRepoError(updated.error()));
    }

    if (!*updated) {
        return std::unexpected(
            ServiceError{drogon::k409Conflict, "task_retry_conflict", "task cannot be retried"});
    }

    try {
        TaskEventHub::instance().publishTaskUpdated(**updated);
        metrics::MetricsRegistry::instance().recordTaskStatus((*updated)->status);
        cache_->del(image_cache::metaKey(userId, id)); // evict cache

        invalidateListCacheFor(userId);
        taskEngine().enqueue(id);
        return ImageGetResult{**updated};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::retryById post-update error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError, "task_retry_failed",
                                            "failed to retry task"});
    }
}

std::expected<ImageBinaryResult, ServiceError> ImageService::getBinaryById(int64_t userId,
                                                                           int64_t id) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    auto image = repo_->findByIdAndUserId(id, userId);
    if (!image) {
        return std::unexpected(mapRepoError(image.error()));
    }

    if (!*image) {
        return std::unexpected(
            ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
    }

    const auto& generation = **image;
    if (!models::canReturnBinary(generation.status, generation.storage_key)) {
        ServiceError err{drogon::k409Conflict, "image_binary_not_ready",
                         "image binary is not ready"};
        err.details["status"] = models::statusToStdString(generation.status);
        return std::unexpected(std::move(err));
    }

    try {
        auto bytes = storage_->getBytes(generation.storage_key);
        if (!bytes) {
            ServiceError err{drogon::k500InternalServerError, "image_storage_read_failed",
                             "failed to load image binary"};
            err.details["storageKey"] = generation.storage_key;
            return std::unexpected(std::move(err));
        }

        return ImageBinaryResult{*bytes, storage_->contentTypeForKey(generation.storage_key)};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::getBinaryById storage error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError,
                                            "image_storage_read_failed",
                                            "failed to load image binary"});
    }
}

std::expected<void, ServiceError> ImageService::deleteById(int64_t userId, int64_t id) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    auto current = repo_->findByIdAndUserId(id, userId);
    if (!current) {
        return std::unexpected(mapRepoError(current.error()));
    }

    if (!*current) {
        return std::unexpected(
            ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
    }

    const auto currentImage = **current;
    if (!models::canDelete(currentImage.status)) {
        ServiceError err{drogon::k400BadRequest, "task_delete_not_allowed",
                         "only completed tasks can be deleted"};
        err.details["status"] = models::statusToStdString(currentImage.status);
        return std::unexpected(std::move(err));
    }

    auto deleted = repo_->deleteByIdAndUserId(id, userId);
    if (!deleted) {
        return std::unexpected(mapRepoError(deleted.error()));
    }

    if (!*deleted) {
        return std::unexpected(ServiceError{drogon::k409Conflict, "task_delete_conflict",
                                            "task cannot be deleted in its current state"});
    }

    try {
        if (!currentImage.storage_key.empty()) {
            try {
                storage_->remove(currentImage.storage_key);
            } catch (const std::exception& ex) {
                spdlog::warn("ImageService::deleteById failed to remove storage object, id={}, "
                             "key={}, reason={}",
                             id, currentImage.storage_key, ex.what());
            }
        }
        cache_->del(image_cache::metaKey(userId, id)); // evict cache
        if (!currentImage.storage_key.empty()) {
            cache_->del(image_cache::presignKey(currentImage.storage_key)); // evict presign cache
        }
        invalidateListCacheFor(userId);
        return {};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::deleteById post-delete error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError, "image_delete_failed",
                                            "failed to delete image"});
    }
}

ImageHealthResult ImageService::checkHealth() const {
    return generation_client_.checkHealth();
}
