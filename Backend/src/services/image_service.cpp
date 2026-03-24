#include "image_service.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <format>
#include <limits>
#include <mutex>
#include <random>
#include <spdlog/spdlog.h>
#include <stop_token>
#include <string>
#include <thread>

#include "Backend.h"
#include "ImageRepo.h"
#include "base64_utils.h"
#include "client.h"
#include "image_storage.h"
#include "task_event_hub.h"
#include "task_state_machine.h"

namespace {

using Clock = std::chrono::system_clock;

struct TaskEngineConfig {
    int workers{1};
    int poll_interval_ms{500};
    long lease_seconds{600};
    int max_retries{3};
    std::string worker_prefix{"backend-worker"};
};

TaskEngineConfig loadTaskEngineConfig() {
    TaskEngineConfig config;

    try {
        const auto& backendConfig = backend::cachedConfig();
        if (backendConfig.contains("task_engine") && backendConfig.at("task_engine").is_object()) {
            const auto& taskEngineConfig = backendConfig.at("task_engine");
            config.workers = (std::max)(0, taskEngineConfig.value("workers", config.workers));
            config.poll_interval_ms =
                (std::max)(100,
                           taskEngineConfig.value("poll_interval_ms", config.poll_interval_ms));
            config.lease_seconds =
                (std::max)(30l, taskEngineConfig.value("lease_seconds", config.lease_seconds));
            config.max_retries =
                (std::max)(0, taskEngineConfig.value("max_retries", config.max_retries));
            config.worker_prefix = taskEngineConfig.value("worker_prefix", config.worker_prefix);
        }
    } catch (const std::exception& ex) {
        spdlog::error("Failed to load task engine config, using defaults. Exception: {}",
                      ex.what());
    } catch (...) {
        spdlog::error("Failed to load task engine config, using defaults. Unknown exception.");
    }
    return config;
}

const TaskEngineConfig& taskEngineConfig() {
    static const TaskEngineConfig config = loadTaskEngineConfig();
    return config;
}

std::vector<std::jthread>& taskWorkers() {
    static std::vector<std::jthread> workers;
    return workers;
}

std::once_flag& workerStartOnce() {
    static std::once_flag onceFlag;
    return onceFlag;
}

// Condition variable for notifying workers when new tasks are enqueued.
// Workers wait on this instead of blindly polling the DB every 500ms.
std::mutex& taskNotifyMutex() {
    static std::mutex mtx;
    return mtx;
}

std::condition_variable& taskNotifyCv() {
    static std::condition_variable cv;
    return cv;
}

void notifyWorkers() {
    taskNotifyCv().notify_one();
}

std::chrono::seconds leaseRenewInterval(long leaseSeconds) {
    return std::chrono::seconds((std::max)(1l, leaseSeconds / 2));
}

std::jthread startLeaseKeeper(const models::ImageGeneration& task, const std::string& workerId,
                              long leaseSeconds) {
    const auto renewEvery = leaseRenewInterval(leaseSeconds);

    return std::jthread([taskId = task.id, userId = task.user_id, workerId, leaseSeconds,
                         renewEvery](std::stop_token stopToken) {
        ImageRepo repo;
        auto nextRenewal = std::chrono::steady_clock::now() + renewEvery;

        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (stopToken.stop_requested()) {
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now < nextRenewal) {
                continue;
            }

            try {
                if (!repo.renewLease(taskId, userId, workerId, leaseSeconds)) {
                    spdlog::warn("lease keeper lost task claim id={}, user_id={}, worker_id={}",
                                 taskId, userId, workerId);
                    break;
                }
            } catch (const std::exception& ex) {
                spdlog::warn(
                    "lease keeper failed to renew lease id={}, user_id={}, worker_id={}, reason={}",
                    taskId, userId, workerId, ex.what());
            } catch (...) {
                spdlog::warn("lease keeper failed to renew lease id={}, user_id={}, worker_id={}, "
                             "reason=unknown",
                             taskId, userId, workerId);
            }

            nextRenewal = now + renewEvery;
        }
    });
}

void cleanupOrphanedStoredImage(const models::ImageGeneration& generation) {
    if (generation.storage_key.empty()) {
        return;
    }

    try {
        ImageStorage storage;
        const bool removed = storage.remove(generation.storage_key);
        if (!removed) {
            spdlog::warn("failed to remove orphaned storage object for task id={}, key={}",
                         generation.id, generation.storage_key);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("failed to remove orphaned storage object for task id={}, key={}, reason={}",
                     generation.id, generation.storage_key, ex.what());
    } catch (...) {
        spdlog::warn(
            "failed to remove orphaned storage object for task id={}, key={}, reason=unknown",
            generation.id, generation.storage_key);
    }
}

std::string buildRequestId() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const auto rand = std::uniform_int_distribution<uint64_t>{}(rng);
    return std::format("img-{}-{:x}", millis, rand);
}

std::string downloadImageAsBase64(const std::string& url, long timeoutSeconds) {
    HttpClient client(timeoutSeconds);
    auto response = client.get(url, {}, true);
    if (!response.ok() || response.body.empty()) {
        return {};
    }

    return utils::encodeToBase64(response.body);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string normalizeStatus(const std::string& rawStatus) {
    const auto status = toLower(rawStatus);

    if (status == "success" || status == "succeeded" || status == "completed" || status == "done" ||
        status == "ok") {
        return "success";
    }

    if (status == "failed" || status == "failure" || status == "error") {
        return "failed";
    }

    if (status == "queued" || status == "pending" || status == "processing" ||
        status == "generating") {
        return "queued";
    }

    return status;
}

std::optional<std::string> getStringField(const nlohmann::json& json, const char* key) {
    if (!json.contains(key) || !json.at(key).is_string()) {
        return std::nullopt;
    }
    return json.at(key).get<std::string>();
}

void mergeRemoteResult(const nlohmann::json& remoteJson, models::ImageGeneration& generation) {
    const nlohmann::json* payload = &remoteJson;
    if (remoteJson.contains("data") && remoteJson.at("data").is_object()) {
        payload = &remoteJson.at("data");
    }

    auto remoteGeneration = models::ImageGeneration::fromJson(*payload);

    if (!remoteGeneration.request_id.empty()) {
        generation.request_id = remoteGeneration.request_id;
    }
    if (!remoteGeneration.status.empty()) {
        generation.status = remoteGeneration.status;
    }
    if (!remoteGeneration.image_url.empty()) {
        generation.image_url = remoteGeneration.image_url;
    }
    if (!remoteGeneration.image_base64.empty()) {
        generation.image_base64 = remoteGeneration.image_base64;
    }
    if (!remoteGeneration.error_message.empty()) {
        generation.error_message = remoteGeneration.error_message;
    }
    if (remoteGeneration.generation_time > 0) {
        generation.generation_time = remoteGeneration.generation_time;
    }

    if (generation.image_url.empty()) {
        if (const auto imageUrl = getStringField(*payload, "image_url")) {
            generation.image_url = *imageUrl;
        } else if (const auto url = getStringField(*payload, "url")) {
            generation.image_url = *url;
        }
    }

    if (generation.image_base64.empty()) {
        if (const auto imageBase64 = getStringField(*payload, "image_base64")) {
            generation.image_base64 = *imageBase64;
        } else if (const auto base64 = getStringField(*payload, "base64")) {
            generation.image_base64 = *base64;
        } else if (const auto image = getStringField(*payload, "image")) {
            if (image->starts_with("http://") || image->starts_with("https://")) {
                generation.image_url = *image;
            } else {
                generation.image_base64 = *image;
            }
        }
    }
}

models::ImageGeneration runRemoteGeneration(models::ImageGeneration generation) {
    long timeoutSeconds = 300;
    std::string serviceUrl;

    try {
        const auto& config = backend::cachedConfig();
        const auto& serviceConfig = config.at("python_service");

        serviceUrl = serviceConfig.at("url").get<std::string>();
        timeoutSeconds = serviceConfig.value("timeout_seconds", 300);

        nlohmann::json modelPayload = {
            {"prompt", generation.prompt},       {"negative_prompt", generation.negative_prompt},
            {"num_steps", generation.num_steps}, {"height", generation.height},
            {"width", generation.width},         {"request_id", generation.request_id}};
        if (generation.seed.has_value()) {
            modelPayload["seed"] = generation.seed.value();
        }

        HttpClient client(timeoutSeconds);
        const auto generateUrl = serviceUrl + "/generate";
        auto response = client.postJson(generateUrl, modelPayload.dump());
        if (!response.ok()) {
            generation.status = "failed";
            generation.failure_code = "python_service_request_failed";
            if (!response.error.empty()) {
                generation.error_message =
                    std::format("python service request failed: {}", response.error);
            } else {
                generation.error_message =
                    std::format("python service request failed, status: {}", response.status_code);
            }
            spdlog::warn("ImageService async call {} failed (status: {}, error: {})", generateUrl,
                         response.status_code, response.error);
        } else if (response.body.empty()) {
            generation.status = "failed";
            generation.failure_code = "python_service_empty_response";
            generation.error_message = "python service returned empty response";
            spdlog::warn("ImageService async call {} returned empty response body", generateUrl);
        } else {
            const auto remoteJson = nlohmann::json::parse(response.body, nullptr, false);
            if (!remoteJson.is_discarded()) {
                mergeRemoteResult(remoteJson, generation);
            } else {
                generation.status = "failed";
                generation.failure_code = "python_service_invalid_json";
                generation.error_message = "python service returned invalid json";
                spdlog::warn("ImageService async call {} returned invalid json", generateUrl);
            }
        }
    } catch (const std::exception& ex) {
        generation.status = "failed";
        generation.failure_code = "python_service_exception";
        generation.error_message = std::format("python service exception: {}", ex.what());
        spdlog::error("ImageService async exception: {}", ex.what());
    } catch (...) {
        generation.status = "failed";
        generation.failure_code = "python_service_unknown_exception";
        generation.error_message = "python service unknown exception";
        spdlog::error("ImageService async unknown exception");
    }

    if (generation.image_base64.empty() && !generation.image_url.empty()) {
        const bool isAbsoluteHttpUrl = generation.image_url.starts_with("http://") ||
                                       generation.image_url.starts_with("https://");
        if (!isAbsoluteHttpUrl && !serviceUrl.empty() && generation.image_url.starts_with("/")) {
            generation.image_url = serviceUrl + generation.image_url;
        }
        generation.image_base64 = downloadImageAsBase64(generation.image_url, timeoutSeconds);
    }

    generation.status = normalizeStatus(generation.status);
    if ((!generation.image_base64.empty() || !generation.image_url.empty()) &&
        generation.status != "failed") {
        generation.status = "success";
        generation.failure_code.clear();
        generation.error_message.clear();
    }

    if (generation.status.empty() || generation.status == "queued") {
        generation.status = "failed";
        generation.failure_code = "missing_image_payload";
        if (generation.error_message.empty()) {
            generation.error_message = "model result did not include image data";
        }
    }

    if (generation.status == "success" || generation.status == "failed") {
        generation.completed_at = std::chrono::system_clock::now();
    }

    return generation;
}

void persistGeneratedImage(models::ImageGeneration& generation) {
    if (generation.status != "success") {
        return;
    }

    if (generation.image_base64.empty()) {
        generation.status = "failed";
        generation.failure_code = "missing_image_payload";
        generation.error_message = "generation succeeded but image payload is missing";
        generation.completed_at = Clock::now();
        return;
    }

    try {
        const auto rawBytes = utils::decodeBase64(generation.image_base64);
        ImageStorage storage;
        const auto stored = storage.store(generation.user_id, generation.request_id, rawBytes);
        generation.storage_key = stored.storage_key;
        generation.image_url = std::format("/api/images/{}/binary", generation.id);
        generation.image_base64.clear();
    } catch (const std::exception& ex) {
        generation.status = "failed";
        generation.failure_code = "storage_write_failed";
        generation.error_message =
            std::format("generation succeeded but failed to store image: {}", ex.what());
        generation.completed_at = Clock::now();
        generation.storage_key.clear();
        generation.image_base64.clear();
        generation.image_url.clear();
    }
}

void workerLoop(std::stop_token stopToken, const std::string& workerId,
                const TaskEngineConfig& config) {
    ImageRepo repo;

    while (!stopToken.stop_requested()) {
        try {
            auto task = repo.claimNextTask(workerId, config.lease_seconds);
            if (!task) {
                // Wait on condition variable instead of blind sleep.
                // Wakes up when create()/retryById() notify, or after
                // poll_interval_ms as fallback. stop_token is checked
                // by the outer while loop on next iteration.
                std::unique_lock lock(taskNotifyMutex());
                taskNotifyCv().wait_for(lock, std::chrono::milliseconds(config.poll_interval_ms));
                continue;
            }

            spdlog::info("task worker claimed task id={}, user_id={}, request_id={}, worker_id={}",
                         task->id, task->user_id, task->request_id, workerId);
            TaskEventHub::instance().publishTaskUpdated(*task);

            auto leaseKeeper = startLeaseKeeper(*task, workerId, config.lease_seconds);
            auto result = runRemoteGeneration(*task);
            persistGeneratedImage(result);
            const bool finished = repo.finishClaimedTask(result);
            leaseKeeper.request_stop();

            if (!finished) {
                cleanupOrphanedStoredImage(result);
                spdlog::warn(
                    "task worker failed to finish claimed task id={}, user_id={}, worker_id={}",
                    task->id, task->user_id, workerId);
            } else {
                TaskEventHub::instance().publishTaskUpdated(result);
            }
        } catch (const std::exception& ex) {
            spdlog::error("task worker exception: {}, worker_id={}", ex.what(), workerId);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
        } catch (...) {
            spdlog::error("task worker unknown exception, worker_id={}", workerId);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
        }
    }
}

void leaseExpiryLoop(std::stop_token stopToken, int intervalSeconds) {
    ImageRepo repo;

    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
        if (stopToken.stop_requested())
            break;

        try {
            int expired = repo.expireLeases();
            if (expired > 0) {
                spdlog::info("lease expiry scanner recovered {} task(s)", expired);
                notifyWorkers();
            }
        } catch (const std::exception& ex) {
            spdlog::error("lease expiry scanner error: {}", ex.what());
        }
    }
}

void ensureWorkersStarted() {
    std::call_once(workerStartOnce(), [] {
        const auto& cfg = taskEngineConfig();
        if (cfg.workers <= 0) {
            spdlog::info("ImageService task engine disabled");
            return;
        }

        auto& workers = taskWorkers();
        // +1 for the lease expiry scanner thread
        workers.reserve(cfg.workers + 1);

        for (int i = 0; i < cfg.workers; ++i) {
            const auto workerId = std::format("{}-{}", cfg.worker_prefix, i + 1);
            workers.emplace_back(workerLoop, workerId, cfg);
        }

        // Lease expiry scanner: runs every 30 seconds to recover stuck tasks
        constexpr int kLeaseExpiryIntervalSeconds = 30;
        workers.emplace_back(leaseExpiryLoop, kLeaseExpiryIntervalSeconds);

        spdlog::info("ImageService task engine started with {} worker(s) + lease scanner",
                     cfg.workers);
    });
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
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
    s.erase(
        std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
        s.end());
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

} // namespace

void ImageService::bootstrapWorkers() {
    ensureWorkersStarted();
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
    generation.status = "queued";
    generation.error_message.clear();
    generation.completed_at = std::nullopt;
    generation.image_url.clear();
    generation.image_base64.clear();
    generation.generation_time = 0;

    try {
        ImageRepo repo;

        if (auto existing = repo.findByRequestIdAndUserId(generation.request_id, userId)) {
            spdlog::info("ImageService create with existing request_id, returning existing "
                         "generation, request_id={}, user_id={}",
                         generation.request_id, userId);
            return ImageCreateResult{*existing};
        }

        generation.id = repo.insert(generation);
        TaskEventHub::instance().publishTaskUpdated(generation);
        ensureWorkersStarted();
        notifyWorkers();
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
        ImageRepo repo;
        auto result = repo.findByUserId(userId, page, size);
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

    const auto target = normalizeStatus(status);

    try {
        ImageRepo repo;
        auto result = repo.findByUserIdAndStatus(userId, target, page, size);
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
        ImageRepo repo;
        auto image = repo.findByIdAndUserId(id, userId);
        if (!image) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
        }

        image->image_base64.clear();
        if (includeImagePayload && !image->storage_key.empty()) {
            try {
                ImageStorage storage;
                image->image_url = storage.presignUrl(image->storage_key);
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
        ImageRepo repo;
        auto current = repo.findByIdAndUserId(id, userId);
        if (!current) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "task_not_found", "task not found"});
        }

        if (!task_state::canCancel(current->status)) {
            ServiceError err{drogon::k400BadRequest, "task_cancel_not_allowed",
                             "task is already completed and cannot be cancelled"};
            err.details["status"] = current->status;
            return std::unexpected(std::move(err));
        }

        models::ImageGeneration updated;
        if (!repo.cancelByIdAndUserId(id, userId, &updated)) {
            return std::unexpected(ServiceError{drogon::k409Conflict, "task_cancel_conflict",
                                                "task cannot be canceled"});
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
        ImageRepo repo;
        auto current = repo.findByIdAndUserId(id, userId);
        if (!current) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "task_not_found", "task not found"});
        }

        if (!task_state::canRetry(current->status, current->retry_count, current->max_retries)) {
            ServiceError err{drogon::k409Conflict, "task_retry_not_allowed",
                             "only failed, timeout or canceled tasks can be retried"};
            err.details["status"] = current->status;
            err.details["retryCount"] = current->retry_count;
            err.details["maxRetries"] = current->max_retries;
            return std::unexpected(std::move(err));
        }

        models::ImageGeneration updated;
        if (!repo.retryByIdAndUserId(id, userId, &updated)) {
            return std::unexpected(ServiceError{drogon::k409Conflict, "task_retry_conflict",
                                                "task cannot be retried"});
        }

        TaskEventHub::instance().publishTaskUpdated(updated);
        ensureWorkersStarted();
        notifyWorkers();
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
        ImageRepo repo;
        auto image = repo.findByIdAndUserId(id, userId);
        if (!image) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
        }

        if (!task_state::canReturnBinary(image->status, image->storage_key)) {
            ServiceError err{drogon::k409Conflict, "image_binary_not_ready",
                             "image binary is not ready"};
            err.details["status"] = image->status;
            return std::unexpected(std::move(err));
        }

        ImageStorage storage;
        auto bytes = storage.getBytes(image->storage_key);
        if (!bytes) {
            ServiceError err{drogon::k500InternalServerError, "image_storage_read_failed",
                             "failed to load image binary"};
            err.details["storageKey"] = image->storage_key;
            return std::unexpected(std::move(err));
        }

        return ImageBinaryResult{*bytes, storage.contentTypeForKey(image->storage_key)};
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
        ImageRepo repo;

        // Must check terminal state before deleting — can't delete in-progress tasks.
        auto current = repo.findByIdAndUserId(id, userId);
        if (!current) {
            return std::unexpected(
                ServiceError{drogon::k404NotFound, "image_not_found", "image not found"});
        }

        if (!task_state::canDelete(current->status)) {
            ServiceError err{drogon::k400BadRequest, "task_delete_not_allowed",
                             "only completed tasks can be deleted"};
            err.details["status"] = current->status;
            return std::unexpected(std::move(err));
        }

        if (!repo.deleteByIdAndUserId(id, userId)) {
            return std::unexpected(ServiceError{drogon::k409Conflict, "task_delete_conflict",
                                                "task cannot be deleted in its current state"});
        }

        // Clean up the stored object to prevent storage leaks.
        if (!current->storage_key.empty()) {
            try {
                ImageStorage storage;
                storage.remove(current->storage_key);
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
    ImageHealthResult result;

    try {
        const auto& config = backend::cachedConfig();
        const auto& serviceConfig = config.at("python_service");

        const auto serviceUrl = serviceConfig.at("url").get<std::string>();
        long timeoutSeconds = serviceConfig.value("timeout_seconds", 30);
        if (timeoutSeconds <= 0) {
            timeoutSeconds = 5;
        }
        timeoutSeconds = (std::min)(timeoutSeconds, 10L);

        HttpClient client(timeoutSeconds);
        const auto response = client.get(serviceUrl + "/health");

        if (!response.ok()) {
            result.status = "unhealthy";
            result.detail = !response.error.empty()
                                ? response.error
                                : std::format("http status {}", response.status_code);
            return result;
        }

        if (response.body.empty()) {
            result.status = "unhealthy";
            result.detail = "empty response body";
            return result;
        }

        const auto healthJson = nlohmann::json::parse(response.body, nullptr, false);
        if (healthJson.is_discarded()) {
            result.status = "unhealthy";
            result.detail = "invalid json response";
            return result;
        }

        const auto remoteStatus = toLower(healthJson.value("status", std::string{}));
        result.model_loaded = healthJson.value("model_loaded", false);

        if (remoteStatus == "healthy" || remoteStatus == "ok" || remoteStatus == "success" ||
            result.model_loaded) {
            result.status = "healthy";
        } else {
            result.status = "unhealthy";
        }

        result.detail = remoteStatus;
        return result;
    } catch (const std::exception& ex) {
        result.status = "unhealthy";
        result.detail = ex.what();
        return result;
    }
}
