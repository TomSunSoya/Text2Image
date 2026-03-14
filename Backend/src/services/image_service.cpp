#include "image_service.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <mutex>
#include <openssl/evp.h>
#include <spdlog/spdlog.h>
#include <string>

#include "Backend.h"
#include "ImageRepo.h"
#include "client.h"
#include "image_storage.h"
#include "task_state_machine.h"

namespace {

using Clock = std::chrono::system_clock;

struct TaskEngineConfig {
    int workers{ 1 };
    int poll_interval_ms{ 500 };
    long lease_seconds{ 600 };
	int max_retries{ 3 };
    std::string worker_prefix{ "backend-worker" };
};

TaskEngineConfig loadTaskEngineConfig()
{
    TaskEngineConfig config;

    try {
        const auto backendConfig = backend::loadConfig();
        if (backendConfig.contains("task_engine") && backendConfig.at("task_engine").is_object()) {
            const auto& taskEngineConfig = backendConfig.at("task_engine");
            config.workers = std::max(1, taskEngineConfig.value("workers", config.workers));
            config.poll_interval_ms = std::max(100, taskEngineConfig.value("poll_interval_ms", config.poll_interval_ms));
            config.lease_seconds = std::max(30l, taskEngineConfig.value("lease_seconds", config.lease_seconds));
            config.max_retries = std::max(0, taskEngineConfig.value("max_retries", config.max_retries));
            config.worker_prefix = taskEngineConfig.value("worker_prefix", config.worker_prefix);
        }
    } catch (const std::exception& ex) {
        spdlog::error("Failed to load task engine config, using defaults. Exception: {}", ex.what());
    } catch (...) {
        spdlog::error("Failed to load task engine config, using defaults. Unknown exception.");
    }
    return config;
}

std::vector<std::jthread>& taskWorkers() {
    static std::vector<std::jthread> workers;
	return workers;
}

std::once_flag &workerStartOnce() {
    static std::once_flag onceFlag;
    return onceFlag;
}

std::string buildRequestId()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "img-" + std::to_string(millis);
}

std::string encodeToBase64(const std::string& bytes)
{
    if (bytes.empty()) {
        return {};
    }

    if (bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return {};
    }

    std::string base64((bytes.size() + 2) / 3 * 4, '\0');
    const int outLen = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(&base64[0]),
        reinterpret_cast<const unsigned char*>(bytes.data()),
        static_cast<int>(bytes.size()));

    if (outLen <= 0) {
        return {};
    }

    base64.resize(static_cast<size_t>(outLen));
    return base64;
}

std::string downloadImageAsBase64(const std::string& url, long timeoutSeconds)
{
    HttpClient client(timeoutSeconds);
    auto response = client.get(url, {}, true);
    if (!response.ok() || response.body.empty()) {
        return {};
    }

    return encodeToBase64(response.body);
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string normalizeStatus(const std::string& rawStatus)
{
    const auto status = toLower(rawStatus);

    if (status == "success" || status == "succeeded" || status == "completed" ||
        status == "done" || status == "ok") {
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

std::optional<std::string> getStringField(const nlohmann::json& json, const char* key)
{
    if (!json.contains(key) || !json.at(key).is_string()) {
        return std::nullopt;
    }
    return json.at(key).get<std::string>();
}

void mergeRemoteResult(const nlohmann::json& remoteJson, models::ImageGeneration& generation)
{
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
            const bool looksLikeUrl = image->rfind("http://", 0) == 0 || image->rfind("https://", 0) == 0;
            if (looksLikeUrl) {
                generation.image_url = *image;
            } else {
                generation.image_base64 = *image;
            }
        }
    }
}

models::ImageGeneration runRemoteGeneration(models::ImageGeneration generation)
{
    long timeoutSeconds = 300;
    std::string serviceUrl;

    try {
        const auto config = backend::loadConfig();
        const auto& serviceConfig = config.at("python_service");

        serviceUrl = serviceConfig.at("url").get<std::string>();
        timeoutSeconds = serviceConfig.value("timeout_seconds", 300);

        nlohmann::json modelPayload = {
            {"prompt", generation.prompt},
            {"negative_prompt", generation.negative_prompt},
            {"num_steps", generation.num_steps},
            {"height", generation.height},
            {"width", generation.width},
            {"request_id", generation.request_id}
        };
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
                generation.error_message = "python service request failed: " + response.error;
            } else {
                generation.error_message = "python service request failed, status: " +
                    std::to_string(response.status_code);
            }
            spdlog::warn("ImageService async call {} failed (status: {}, error: {})",
                         generateUrl,
                         response.status_code,
                         response.error);
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
        generation.error_message = std::string("python service exception: ") + ex.what();
        spdlog::error("ImageService async exception: {}", ex.what());
    } catch (...) {
        generation.status = "failed";
        generation.failure_code = "python_service_unknown_exception";
        generation.error_message = "python service unknown exception";
        spdlog::error("ImageService async unknown exception");
    }

    if (generation.image_base64.empty() && !generation.image_url.empty()) {
        const bool isAbsoluteHttpUrl = generation.image_url.rfind("http://", 0) == 0
            || generation.image_url.rfind("https://", 0) == 0;
        if (!isAbsoluteHttpUrl && !serviceUrl.empty() && generation.image_url.rfind("/", 0) == 0) {
            generation.image_url = serviceUrl + generation.image_url;
        }
        generation.image_base64 = downloadImageAsBase64(generation.image_url, timeoutSeconds);
    }

    generation.status = normalizeStatus(generation.status);
    if ((!generation.image_base64.empty() || !generation.image_url.empty()) && generation.status != "failed") {
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

void persistGeneratedImage(models::ImageGeneration& generation)
{
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
        ImageStorage storage;
        const auto stored = storage.storeBase64(generation.id, generation.request_id, generation.image_base64);
        generation.storage_key = stored.storage_key;
        generation.image_url = stored.image_url;
        generation.image_base64.clear();
    } catch (const std::exception& ex) {
        generation.status = "failed";
        generation.failure_code = "storage_write_failed";
        generation.error_message = std::string("generation succeeded but failed to store image: ") + ex.what();
        generation.completed_at = Clock::now();
        generation.storage_key.clear();
        generation.image_base64.clear();
        generation.image_url.clear();
    }
}

void processQueuedGeneration(const models::ImageGeneration& task)
{
    ImageRepo repo;

    if (!repo.updateStatusAndError(task.id, task.user_id, "generating", "")) {
        spdlog::warn("ImageService async failed to mark generating, id={}, user_id={}", task.id, task.user_id);
    }

    auto result = runRemoteGeneration(task);
    if (result.completed_at.has_value() == false && (result.status == "success" || result.status == "failed")) {
        result.completed_at = std::chrono::system_clock::now();
    }

    persistGeneratedImage(result);

    if (!repo.updateGenerationResult(result.id,
                                     result.user_id,
                                     result.status,
                                     result.image_url,
                                     result.image_base64,
                                     result.error_message,
                                     result.generation_time,
                                     result.failure_code,
                                     result.thumbnail_url,
                                     result.storage_key,
                                     result.completed_at)) {
        spdlog::warn("ImageService async failed to persist result, id={}, user_id={}", result.id, result.user_id);
    }
}

void workerLoop(std::stop_token stopToken, const std::string& workerId, const TaskEngineConfig& config)
{
    ImageRepo repo;

    while (!stopToken.stop_requested()) {
        try {
			auto task = repo.claimNextTask(workerId, config.lease_seconds);
            if (!task) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
				continue;
            }

			spdlog::info("task worker claimed task id = {}, user_id = {}, request_id = {}, worker_id = {}", task->id, task->user_id, task->request_id, workerId);

			auto result = runRemoteGeneration(*task);
			persistGeneratedImage(result);
            if (!repo.finishClaimedTask(result))
				spdlog::warn("task worker failed to finish claimed task id = {}, user_id = {}, worker_id = {}", task->id, task->user_id, workerId);
		}
		catch (const std::exception& ex) {
            spdlog::error("task worker exception: {}, worker_id = {}", ex.what(), workerId);
			std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
        } catch (...) {
            spdlog::error("task worker unknown exception, worker_id = {}", workerId);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
        }
    }
}

void ensureWorkersStarted() {
    std::call_once(workerStartOnce(), [] {
		const auto cfg = loadTaskEngineConfig();
		auto& workers = taskWorkers();
		workers.reserve(cfg.workers);

        for (int i = 0; i < cfg.workers; ++i) {
			const auto workerId = cfg.worker_prefix + "-" + std::to_string(i + 1);
            workers.emplace_back(workerLoop, workerId, cfg);
        }

		spdlog::info("ImageService task engine started with {} worker(s)", cfg.workers);
	});
}

} // namespace

void ImageService::bootstrapWorkers()
{
	ensureWorkersStarted();
}

std::optional<ImageCreateResult> ImageService::create(int64_t userId,
                                                      const nlohmann::json& payload,
                                                      ServiceError& error) const
{
    if (userId <= 0) {
        error.status = drogon::k401Unauthorized;
        error.code = "unauthorized";
        error.message = "unauthorized";
        return std::nullopt;
    }

    models::ImageGeneration generation = models::ImageGeneration::fromJson(payload);
    if (generation.prompt.empty()) {
        error.status = drogon::k400BadRequest;
        error.code = "prompt_required";
        error.message = "prompt is required";
        return std::nullopt;
    }

    generation.user_id = userId;
    generation.created_at = std::chrono::system_clock::now();
    generation.request_id = generation.request_id.empty() ? buildRequestId() : generation.request_id;
    generation.status = "queued";
    generation.error_message.clear();
    generation.completed_at = std::nullopt;
    generation.image_url.clear();
    generation.image_base64.clear();
    generation.generation_time = 0;

    try {
        ImageRepo repo;

		if (auto existing = repo.findByRequestIdAndUserId(generation.request_id, userId)) {
            spdlog::info("ImageService create with existing request_id, returning existing generation, request_id={}, user_id={}", generation.request_id, userId);
            return ImageCreateResult{*existing};
        }

        generation.id = repo.insert(generation);
		ensureWorkersStarted();
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::create persist error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.code = "image_history_persist_failed";
        error.message = "failed to persist image history";
        return std::nullopt;
    }

    return ImageCreateResult{generation};
}

std::optional<ImageListResult> ImageService::listMy(int64_t userId,
                                                    int page,
                                                    int size,
                                                    ServiceError& error) const
{
    if (userId <= 0) {
        error.status = drogon::k401Unauthorized;
        error.code = "unauthorized";
        error.message = "unauthorized";
        return std::nullopt;
    }

    try {
        ImageRepo repo;
        auto content = repo.findByUserId(userId, page, size);
        const auto total = repo.countByUserId(userId);
        return ImageListResult{std::move(content), total};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::listMy error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.code = "image_history_load_failed";
        error.message = "failed to load image history";
        return std::nullopt;
    }
}

std::optional<ImageListResult> ImageService::listMyByStatus(int64_t userId,
                                                            const std::string& status,
                                                            int page,
                                                            int size,
                                                            ServiceError& error) const
{
    if (userId <= 0) {
        error.status = drogon::k401Unauthorized;
        error.code = "unauthorized";
        error.message = "unauthorized";
        return std::nullopt;
    }

    const auto target = normalizeStatus(status);

    try {
        ImageRepo repo;
        auto content = repo.findByUserIdAndStatus(userId, target, page, size);
        const auto total = repo.countByUserIdAndStatus(userId, target);
        return ImageListResult{std::move(content), total};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::listMyByStatus error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.code = "image_history_load_failed";
        error.message = "failed to load image history";
        return std::nullopt;
    }
}

std::optional<ImageGetResult> ImageService::getById(int64_t userId,
                                                    int64_t id,
                                                    ServiceError& error,
                                                    bool includeImagePayload) const
{
    if (userId <= 0) {
        error.status = drogon::k401Unauthorized;
        error.code = "unauthorized";
        error.message = "unauthorized";
        return std::nullopt;
    }

    try {
        ImageRepo repo;
        auto image = repo.findByIdAndUserId(id, userId);
        if (!image) {
            error.status = drogon::k404NotFound;
            error.code = "image_not_found";
            error.message = "image not found";
            return std::nullopt;
        }

        if (!includeImagePayload) {
            image->image_base64.clear();
        } else if (image->image_base64.empty() && !image->storage_key.empty()) {
            ImageStorage storage;
            std::string storageError;
            if (auto base64 = storage.loadBase64(image->storage_key, storageError)) {
                image->image_base64 = *base64;
            } else {
                spdlog::warn("ImageService::getById failed to load image payload, id={}, user_id={}, reason={}",
                             id,
                             userId,
                             storageError);
            }
        }

        return ImageGetResult{*image};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::getById error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.code = "image_lookup_failed";
        error.message = "failed to load image";
        return std::nullopt;
    }
}

std::optional<ImageGetResult> ImageService::cancelById(int64_t userId, int64_t id, ServiceError& error) const
{
    if (userId <= 0)
    {
        error.status = drogon::k401Unauthorized;
        error.code = "unauthorized";
        error.message = "unauthorized";
		return std::nullopt;
    }

    try {
        ImageRepo repo;
		auto current = repo.findByIdAndUserId(id, userId);
        if (!current) {
            error.status = drogon::k404NotFound;
            error.code = "task_not_found";
            error.message = "task not found";
			return std::nullopt;
        }

        if (!task_state::canCancel(current->status)) {
            error.status = drogon::k400BadRequest;
            error.code = "task_cancel_not_allowed";
            error.message = "task is already completed and cannot be cancelled";
            error.details["status"] = current->status;
            return std::nullopt;
        }

		models::ImageGeneration updated;
		if (!repo.cancelByIdAndUserId(id, userId, &updated)) {
            error.status = drogon::k409Conflict;
            error.code = "task_cancel_conflict";
            error.message = "task cannot be canceled";
            return std::nullopt;
        }

		return ImageGetResult{ updated };
	}
	catch (const std::exception& ex) {
        spdlog::error("ImageService::cancelById error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.code = "task_cancel_failed";
        error.message = "failed to cancel task";
        return std::nullopt;
    }
}

std::optional<ImageGetResult> ImageService::retryById(int64_t userId, int64_t id, ServiceError& error) const
{
    if (userId <= 0)
    {
        error.status = drogon::k401Unauthorized;
        error.code = "unauthorized";
        error.message = "unauthorized";
        return std::nullopt;
    }

    try {
		ImageRepo repo;
		auto current = repo.findByIdAndUserId(id, userId);
        if (!current) {
            error.status = drogon::k404NotFound;
            error.code = "task_not_found";
            error.message = "task not found";
            return std::nullopt;
		}

		if (!task_state::canRetry(current->status, current->retry_count, current->max_retries)) {
            error.status = drogon::k409Conflict;
            error.code = "task_retry_not_allowed";
            error.message = "only failed, timeout or canceled tasks can be retried";
            error.details["status"] = current->status;
            error.details["retryCount"] = current->retry_count;
            error.details["maxRetries"] = current->max_retries;
            return std::nullopt;
        }

		models::ImageGeneration updated;
		if (!repo.retryByIdAndUserId(id, userId, &updated)) {
            error.status = drogon::k409Conflict;
            error.code = "task_retry_conflict";
            error.message = "task cannot be retried";
            return std::nullopt;
        }

		ensureWorkersStarted();
		return ImageGetResult{ updated };
	}
	catch (const std::exception& ex) {
        spdlog::error("ImageService::retryById error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.code = "task_retry_failed";
        error.message = "failed to retry task";
        return std::nullopt;
    }
}

std::optional<ImageBinaryResult> ImageService::getBinaryById(int64_t userId, int64_t id, ServiceError& error) const
{
    if (userId <= 0) {
        error.status = drogon::k401Unauthorized;
        error.code = "unauthorized";
        error.message = "unauthorized";
        return std::nullopt;
    }

    try {
        ImageRepo repo;
        auto image = repo.findByIdAndUserId(id, userId);
        if (!image) {
            error.status = drogon::k404NotFound;
            error.code = "image_not_found";
            error.message = "image not found";
            return std::nullopt;
        }

        if (!task_state::canReturnBinary(image->status, image->storage_key)) {
            error.status = drogon::k409Conflict;
            error.code = "image_binary_not_ready";
            error.message = "image binary is not ready";
            error.details["status"] = image->status;
            return std::nullopt;
        }

        ImageStorage storage;
        std::string storageError;
        auto bytes = storage.loadBytes(image->storage_key, storageError);
        if (!bytes) {
            error.status = drogon::k500InternalServerError;
            error.code = "image_storage_read_failed";
            error.message = "failed to load image binary";
            error.details["storageKey"] = image->storage_key;
            error.details["reason"] = storageError;
            return std::nullopt;
        }

        return ImageBinaryResult{*bytes, storage.contentTypeForKey(image->storage_key)};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::getBinaryById error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.code = "image_binary_lookup_failed";
        error.message = "failed to load image binary";
        return std::nullopt;
    }
}

bool ImageService::deleteById(int64_t userId, int64_t id, ServiceError& error) const
{
    if (userId <= 0) {
        error.status = drogon::k401Unauthorized;
        error.code = "unauthorized";
        error.message = "unauthorized";
        return false;
    }

    try {
        ImageRepo repo;
        if (!repo.deleteByIdAndUserId(id, userId)) {
            error.status = drogon::k404NotFound;
            error.code = "image_not_found";
            error.message = "image not found";
            return false;
        }

        return true;
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::deleteById error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.code = "image_delete_failed";
        error.message = "failed to delete image";
        return false;
    }
}

ImageHealthResult ImageService::checkHealth() const
{
    ImageHealthResult result;

    try {
        const auto config = backend::loadConfig();
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
                : ("http status " + std::to_string(response.status_code));
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

        if (remoteStatus == "healthy" || remoteStatus == "ok" || remoteStatus == "success" || result.model_loaded) {
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
