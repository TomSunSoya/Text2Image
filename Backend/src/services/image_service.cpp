#include "image_service.h"

#include <chrono>

#include "Backend.h"
#include "client.h"

namespace {

std::string buildRequestId()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "img-" + std::to_string(millis);
}

}

std::optional<ImageCreateResult> ImageService::create(const nlohmann::json& payload,
                                                      ServiceError& error) const
{
    models::ImageGeneration generation = models::ImageGeneration::fromJson(payload);
    if (generation.prompt.empty()) {
        error.status = drogon::k400BadRequest;
        error.message = "prompt is required";
        return std::nullopt;
    }

    generation.created_at = std::chrono::system_clock::now();
    generation.request_id = generation.request_id.empty() ? buildRequestId() : generation.request_id;
    generation.status = "queued";

    try {
        const auto config = backend::loadConfig();
        const auto& serviceConfig = config.at("python_service");

        const auto url = serviceConfig.at("url").get<std::string>();
        const auto timeout = serviceConfig.value("timeout_seconds", 120);

        HttpClient client(timeout);
        auto response = client.postJson(url + "/generate", payload.dump());
        if (response.ok() && !response.body.empty()) {
            const auto remoteJson = nlohmann::json::parse(response.body, nullptr, false);
            if (!remoteJson.is_discarded()) {
                auto remoteGeneration = models::ImageGeneration::fromJson(remoteJson);
                if (!remoteGeneration.request_id.empty()) {
                    generation.request_id = remoteGeneration.request_id;
                }
                if (!remoteGeneration.status.empty()) {
                    generation.status = remoteGeneration.status;
                }
                generation.image_url = remoteGeneration.image_url;
                generation.image_base64 = remoteGeneration.image_base64;
                generation.error_message = remoteGeneration.error_message;
                generation.generation_time = remoteGeneration.generation_time;
            }
        }
    } catch (...) {
        // Local fallback keeps the request accepted even when the Python service is unavailable.
    }

    return ImageCreateResult{generation};
}
