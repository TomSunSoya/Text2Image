#include "services/generation_client.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Backend.h"
#include "models/failure_code.h"
#include "models/image_storage.h"
#include "models/task_status.h"
#include "services/client.h"

namespace {

using Clock = std::chrono::system_clock;

std::string downloadImageBytes(const std::string& url, long timeoutSeconds,
                               const IHttpClient& httpClient) {
    auto response = httpClient.get(url, timeoutSeconds, {}, true);
    if (!response.ok() || response.body.empty()) {
        return {};
    }

    return response.body;
}

std::string toLower(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
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

    if (const auto rawStatus = getStringField(*payload, "status")) {
        if (const auto normalizedStatus = models::normalizeTaskStatus(*rawStatus);
            normalizedStatus != models::TaskStatus::Unknown) {
            generation.status = normalizedStatus;
        }
    } else if (remoteGeneration.status != models::TaskStatus::Unknown) {
        generation.status = remoteGeneration.status;
    }

    if (!remoteGeneration.image_url.empty()) {
        generation.image_url = remoteGeneration.image_url;
    }
    if (!remoteGeneration.error_message.empty()) {
        generation.error_message = remoteGeneration.error_message;
    }
    if (remoteGeneration.generation_time > 0) {
        generation.generation_time = remoteGeneration.generation_time;
    }

    if (generation.image_url.empty()) {
        generation.image_url = getStringField(*payload, "image_url")
                                   .or_else([&]() { return getStringField(*payload, "url"); })
                                   .value_or(std::string{});
    }

    if (generation.image_url.empty()) {
        if (const auto image = getStringField(*payload, "image")) {
            if (image->starts_with("http://") || image->starts_with("https://")) {
                generation.image_url = *image;
            }
        }
    }
}

void persistGeneratedImage(models::ImageGeneration& generation) {
    if (generation.status != models::TaskStatus::Success) {
        return;
    }

    if (generation.image_bytes.empty()) {
        generation.status = models::TaskStatus::Failed;
        generation.failure_code = std::string(models::failure::kMissingImagePayload);
        generation.error_message = "generation succeeded but image binary is missing";
        generation.completed_at = Clock::now();
        return;
    }

    try {
        ImageStorage storage;
        const auto stored =
            storage.store(generation.user_id, generation.request_id, generation.image_bytes);
        generation.storage_key = stored.storage_key;
        generation.image_url = std::format("/api/images/{}/binary", generation.id);
        generation.image_bytes.clear();
    } catch (const std::exception& ex) {
        generation.status = models::TaskStatus::Failed;
        generation.failure_code = std::string(models::failure::kStorageWriteFailed);
        generation.error_message =
            std::format("generation succeeded but failed to store image: {}", ex.what());
        generation.completed_at = Clock::now();
        generation.storage_key.clear();
        generation.image_bytes.clear();
        generation.image_url.clear();
    }
}

} // namespace

GenerationClient::GenerationClient() : httpClient_(std::make_shared<HttpClient>()) {}

GenerationClient::GenerationClient(std::shared_ptr<IHttpClient> httpClient)
    : httpClient_(std::move(httpClient)) {
    if (!httpClient_) {
        throw std::invalid_argument("GenerationClient httpClient must not be null");
    }
}

models::ImageGeneration GenerationClient::generate(models::ImageGeneration generation) const {
    long timeoutSeconds = 900;
    std::string serviceUrl;

    try {
        const auto& config = backend::cachedConfig();
        const auto& serviceConfig = config.at("python_service");

        serviceUrl = serviceConfig.at("url").get<std::string>();
        timeoutSeconds = serviceConfig.value("timeout_seconds", 900);

        nlohmann::json modelPayload = {
            {"prompt", generation.prompt},       {"negative_prompt", generation.negative_prompt},
            {"num_steps", generation.num_steps}, {"height", generation.height},
            {"width", generation.width},         {"request_id", generation.request_id}};
        if (generation.seed.has_value()) {
            modelPayload["seed"] = generation.seed.value();
        }

        const auto generateUrl = serviceUrl + "/generate";
        auto response = httpClient_->postJson(generateUrl, timeoutSeconds, modelPayload.dump());
        if (!response.ok()) {
            generation.status = models::TaskStatus::Failed;
            generation.failure_code = std::string(models::failure::kPythonServiceRequestFailed);
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
            generation.status = models::TaskStatus::Failed;
            generation.failure_code = std::string(models::failure::kPythonServiceEmptyResponse);
            generation.error_message = "python service returned empty response";
            spdlog::warn("ImageService async call {} returned empty response body", generateUrl);
        } else {
            const auto remoteJson = nlohmann::json::parse(response.body, nullptr, false);
            if (!remoteJson.is_discarded()) {
                mergeRemoteResult(remoteJson, generation);
            } else {
                generation.status = models::TaskStatus::Failed;
                generation.failure_code = std::string(models::failure::kPythonServiceInvalidJson);
                generation.error_message = "python service returned invalid json";
                spdlog::warn("ImageService async call {} returned invalid json", generateUrl);
            }
        }
    } catch (const std::exception& ex) {
        generation.status = models::TaskStatus::Failed;
        generation.failure_code = std::string(models::failure::kPythonServiceException);
        generation.error_message = std::format("python service exception: {}", ex.what());
        spdlog::error("ImageService async exception: {}", ex.what());
    } catch (...) {
        generation.status = models::TaskStatus::Failed;
        generation.failure_code = std::string(models::failure::kPythonServiceUnknownException);
        generation.error_message = "python service unknown exception";
        spdlog::error("ImageService async unknown exception");
    }

    if (!generation.image_url.empty()) {
        const bool isAbsoluteHttpUrl = generation.image_url.starts_with("http://") ||
                                       generation.image_url.starts_with("https://");
        if (!isAbsoluteHttpUrl && !serviceUrl.empty() && generation.image_url.starts_with("/")) {
            generation.image_url = serviceUrl + generation.image_url;
        }
        generation.image_bytes =
            downloadImageBytes(generation.image_url, timeoutSeconds, *httpClient_);
    }

    if (!generation.image_bytes.empty() && generation.status != models::TaskStatus::Failed) {
        generation.status = models::TaskStatus::Success;
        generation.failure_code.clear();
        generation.error_message.clear();
    }

    if (generation.status == models::TaskStatus::Unknown ||
        generation.status == models::TaskStatus::Pending ||
        generation.status == models::TaskStatus::Queued ||
        generation.status == models::TaskStatus::Generating) {
        generation.status = models::TaskStatus::Failed;
        generation.failure_code = std::string(models::failure::kMissingImagePayload);
        if (generation.error_message.empty()) {
            generation.error_message = "model result did not include image data";
        }
    }

    if (models::isTerminal(generation.status)) {
        generation.completed_at = std::chrono::system_clock::now();
    }

    persistGeneratedImage(generation);
    return generation;
}

ImageHealthResult GenerationClient::checkHealth() const {
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

        const auto response = httpClient_->get(serviceUrl + "/health", timeoutSeconds);

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

void GenerationClient::cleanupOrphanedStoredImage(const models::ImageGeneration& generation) {
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
