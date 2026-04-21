#include "services/image_service.h"

#include <cctype>
#include <chrono>
#include <format>
#include <optional>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "database/ImageRepo.h"
#include "models/image_storage.h"
#include "models/task_status.h"
#include "services/generation_client.h"
#include "services/redis_client.h"
#include "services/task_engine.h"
#include "services/task_event_hub.h"

namespace {

TaskEngine& taskEngine() {
    static TaskEngine engine;
    return engine;
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

void presignListImages(const IImageStorage& storage,
                       std::vector<models::ImageGeneration>& images) {
    for (auto& img :
         images | std::views::filter([](const auto& i) { return !i.storage_key.empty(); })) {
        try {
            img.image_url = storage.presignUrl(img.storage_key);
        } catch (const std::exception& ex) {
            spdlog::error("presignListImages: failed to presign storage_key='{}': {}",
                          img.storage_key, ex.what());
        } catch (...) {
            spdlog::error("presignListImages: unknown error for storage_key='{}'",
                          img.storage_key);
        }
    }
}

} // namespace

ImageService::ImageService()
    : repo_(std::make_shared<ImageRepo>()), storage_(std::make_shared<ImageStorage>()) {}

ImageService::ImageService(std::shared_ptr<IImageRepo> repo, std::shared_ptr<IImageStorage> storage)
    : repo_(std::move(repo)), storage_(std::move(storage)) {
    if (!repo_) {
        throw std::invalid_argument("ImageService: repo must not be null");
    }
    if (!storage_) {
        throw std::invalid_argument("ImageService: storage must not be null");
    }
}

void ImageService::bootstrapWorkers() {
    taskEngine().bootstrap();
}

std::expected<ImageCreateResult, ServiceError>
ImageService::create(int64_t userId, const nlohmann::json& payload) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    models::ImageGeneration generation = models::ImageGeneration::fromJson(payload);

    if (auto validationError = validateGenerationParams(generation)) {
        return std::unexpected(std::move(*validationError));
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

    try {
        if (auto existing = repo_->findByRequestIdAndUserId(generation.request_id, userId)) {
            spdlog::info("ImageService create with existing request_id, returning existing "
                         "generation, request_id={}, user_id={}",
                         generation.request_id, userId);
            return ImageCreateResult{*existing};
        }

        generation.id = repo_->insert(generation);
        TaskEventHub::instance().publishTaskUpdated(generation);
        taskEngine().enqueue(generation.id);
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::create persist error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError,
                                            "image_history_persist_failed",
                                            "failed to persist image history"});
    }

    return ImageCreateResult{generation};
}

std::expected<ImageListResult, ServiceError> ImageService::listMy(int64_t userId, int page,
                                                                  int size) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    try {
        auto result = repo_->findByUserId(userId, page, size);
        presignListImages(*storage_, result.content);
        return ImageListResult{std::move(result.content), result.total_elements};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::listMy error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError,
                                            "image_history_load_failed",
                                            "failed to load image history"});
    }
}

std::expected<ImageListResult, ServiceError>
ImageService::listMyByStatus(int64_t userId, const std::string& status, int page, int size) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    const auto target = models::normalizeTaskStatus(status);

    try {
        auto result = repo_->findByUserIdAndStatus(userId, target, page, size);
        presignListImages(*storage_, result.content);
        return ImageListResult{std::move(result.content), result.total_elements};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::listMyByStatus error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError,
                                            "image_history_load_failed",
                                            "failed to load image history"});
    }
}

std::expected<ImageGetResult, ServiceError> ImageService::getById(int64_t userId, int64_t id,
                                                                  bool includeImagePayload) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    try {
        auto image = repo_->findByIdAndUserId(id, userId);
        if (!image) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
        }

        if (includeImagePayload && !image->storage_key.empty()) {
            try {
                image->image_url = storage_->presignUrl(image->storage_key);
            } catch (const std::exception& ex) {
                spdlog::warn("ImageService::getById failed to generate presigned URL, id={}, "
                             "user_id={}, reason={}",
                             id, userId, ex.what());
            }
        }

        return ImageGetResult{*image};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::getById error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError, "image_lookup_failed",
                                            "failed to load image"});
    }
}

std::expected<ImageGetResult, ServiceError> ImageService::cancelById(int64_t userId,
                                                                     int64_t id) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    try {
        auto current = repo_->findByIdAndUserId(id, userId);
        if (!current) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "task_not_found", "task not found"});
        }

        if (!models::canCancel(current->status)) {
            ServiceError err{drogon::k400BadRequest, "task_cancel_not_allowed",
                             "task is already completed and cannot be cancelled"};
            err.details["status"] = models::statusToStdString(current->status);
            return std::unexpected(std::move(err));
        }

        models::ImageGeneration updated;
        if (!repo_->cancelByIdAndUserId(id, userId, &updated)) {
            return std::unexpected(ServiceError{drogon::k409Conflict, "task_cancel_conflict",
                                                "task cannot be canceled"});
        }

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

        TaskEventHub::instance().publishTaskUpdated(updated);
        return ImageGetResult{updated};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::cancelById error: {}", ex.what());
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

    try {
        auto current = repo_->findByIdAndUserId(id, userId);
        if (!current) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "task_not_found", "task not found"});
        }

        if (!models::canRetry(current->status, current->retry_count, current->max_retries)) {
            ServiceError err{drogon::k409Conflict, "task_retry_not_allowed",
                             "only failed, timeout or canceled tasks can be retried"};
            err.details["status"] = models::statusToStdString(current->status);
            err.details["retryCount"] = current->retry_count;
            err.details["maxRetries"] = current->max_retries;
            return std::unexpected(std::move(err));
        }

        models::ImageGeneration updated;
        if (!repo_->retryByIdAndUserId(id, userId, &updated)) {
            return std::unexpected(ServiceError{drogon::k409Conflict, "task_retry_conflict",
                                                "task cannot be retried"});
        }

        TaskEventHub::instance().publishTaskUpdated(updated);
        taskEngine().enqueue(id);
        return ImageGetResult{updated};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::retryById error: {}", ex.what());
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

    try {
        auto image = repo_->findByIdAndUserId(id, userId);
        if (!image) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
        }

        if (!models::canReturnBinary(image->status, image->storage_key)) {
            ServiceError err{drogon::k409Conflict, "image_binary_not_ready",
                             "image binary is not ready"};
            err.details["status"] = models::statusToStdString(image->status);
            return std::unexpected(std::move(err));
        }

        auto bytes = storage_->getBytes(image->storage_key);
        if (!bytes) {
            ServiceError err{drogon::k500InternalServerError, "image_storage_read_failed",
                             "failed to load image binary"};
            err.details["storageKey"] = image->storage_key;
            return std::unexpected(std::move(err));
        }

        return ImageBinaryResult{*bytes, storage_->contentTypeForKey(image->storage_key)};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::getBinaryById error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError,
                                            "image_binary_lookup_failed",
                                            "failed to load image binary"});
    }
}

std::expected<void, ServiceError> ImageService::deleteById(int64_t userId, int64_t id) const {
    if (userId <= 0) {
        return std::unexpected(
            ServiceError{drogon::k401Unauthorized, "unauthorized", "unauthorized"});
    }

    try {
        auto current = repo_->findByIdAndUserId(id, userId);
        if (!current) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
        }

        if (!models::canDelete(current->status)) {
            ServiceError err{drogon::k400BadRequest, "task_delete_not_allowed",
                             "only completed tasks can be deleted"};
            err.details["status"] = models::statusToStdString(current->status);
            return std::unexpected(std::move(err));
        }

        if (!repo_->deleteByIdAndUserId(id, userId)) {
            return std::unexpected(ServiceError{drogon::k409Conflict, "task_delete_conflict",
                                                "task cannot be deleted in its current state"});
        }

        if (!current->storage_key.empty()) {
            try {
                storage_->remove(current->storage_key);
            } catch (const std::exception& ex) {
                spdlog::warn("ImageService::deleteById failed to remove storage object, id={}, "
                             "key={}, reason={}",
                             id, current->storage_key, ex.what());
            }
        }

        return {};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::deleteById error: {}", ex.what());
        return std::unexpected(ServiceError{drogon::k500InternalServerError, "image_delete_failed",
                                            "failed to delete image"});
    }
}

ImageHealthResult ImageService::checkHealth() const {
    return GenerationClient::checkHealth();
}
