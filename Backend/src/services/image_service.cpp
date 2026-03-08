#include "image_service.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <openssl/evp.h>
#include <spdlog/spdlog.h>
#include <string>

#include "Backend.h"
#include "ImageRepo.h"
#include "client.h"

namespace {

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

} // namespace

std::optional<ImageCreateResult> ImageService::create(int64_t userId,
                                                      const nlohmann::json& payload,
                                                      ServiceError& error) const
{
    if (userId <= 0) {
        error.status = drogon::k401Unauthorized;
        error.message = "unauthorized";
        return std::nullopt;
    }

    models::ImageGeneration generation = models::ImageGeneration::fromJson(payload);
    if (generation.prompt.empty()) {
        error.status = drogon::k400BadRequest;
        error.message = "prompt is required";
        return std::nullopt;
    }

    generation.user_id = userId;
    generation.created_at = std::chrono::system_clock::now();
    generation.request_id = generation.request_id.empty() ? buildRequestId() : generation.request_id;
    generation.status = "queued";

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
            if (!response.error.empty()) {
                generation.error_message = "python service request failed: " + response.error;
            } else {
                generation.error_message = "python service request failed, status: " +
                    std::to_string(response.status_code);
            }
            spdlog::warn("ImageService::create call {} failed (status: {}, error: {})",
                         generateUrl,
                         response.status_code,
                         response.error);
        } else if (response.body.empty()) {
            generation.status = "failed";
            generation.error_message = "python service returned empty response";
            spdlog::warn("ImageService::create call {} returned empty response body", generateUrl);
        } else {
            const auto remoteJson = nlohmann::json::parse(response.body, nullptr, false);
            if (!remoteJson.is_discarded()) {
                mergeRemoteResult(remoteJson, generation);
            } else {
                generation.status = "failed";
                generation.error_message = "python service returned invalid json";
                spdlog::warn("ImageService::create call {} returned invalid json: {}",
                             generateUrl,
                             response.body);
            }
        }
    } catch (const std::exception& ex) {
        generation.status = "failed";
        generation.error_message = std::string("python service exception: ") + ex.what();
        spdlog::error("ImageService::create exception: {}", ex.what());
    } catch (...) {
        generation.status = "failed";
        generation.error_message = "python service unknown exception";
        spdlog::error("ImageService::create unknown exception");
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
    }
    if (generation.status.empty()) {
        generation.status = "queued";
    }

    if (generation.status == "success" || generation.status == "failed") {
        generation.completed_at = std::chrono::system_clock::now();
    }

    try {
        ImageRepo repo;
        generation.id = repo.insert(generation);
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::create persist error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
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
        error.message = "failed to load image history";
        return std::nullopt;
    }
}

std::optional<ImageGetResult> ImageService::getById(int64_t userId, int64_t id, ServiceError& error) const
{
    if (userId <= 0) {
        error.status = drogon::k401Unauthorized;
        error.message = "unauthorized";
        return std::nullopt;
    }

    try {
        ImageRepo repo;
        auto image = repo.findByIdAndUserId(id, userId);
        if (!image) {
            error.status = drogon::k404NotFound;
            error.message = "image not found";
            return std::nullopt;
        }

        return ImageGetResult{*image};
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::getById error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.message = "failed to load image";
        return std::nullopt;
    }
}

bool ImageService::deleteById(int64_t userId, int64_t id, ServiceError& error) const
{
    if (userId <= 0) {
        error.status = drogon::k401Unauthorized;
        error.message = "unauthorized";
        return false;
    }

    try {
        ImageRepo repo;
        if (!repo.deleteByIdAndUserId(id, userId)) {
            error.status = drogon::k404NotFound;
            error.message = "image not found";
            return false;
        }

        return true;
    } catch (const std::exception& ex) {
        spdlog::error("ImageService::deleteById error: {}", ex.what());
        error.status = drogon::k500InternalServerError;
        error.message = "failed to delete image";
        return false;
    }
}
