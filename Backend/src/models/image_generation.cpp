#include "image_generation.h"
#include <iomanip>
#include <sstream>

namespace models {

    static std::string timeToString(const std::chrono::system_clock::time_point& tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm{};
        localtime_s(&tm, &time);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    nlohmann::json ImageGeneration::toJson() const {
        nlohmann::json j = {
            {"id", id},
            {"requestId", request_id},
            {"prompt", prompt},
            {"negativePrompt", negative_prompt},
            {"numSteps", num_steps},
            {"height", height},
            {"width", width},
            {"status", status},
            {"imageUrl", image_url},
            {"imageBase64", image_base64},
            {"errorMessage", error_message},
            {"generationTime", generation_time},
            {"createdAt", timeToString(created_at)}
        };

        if (seed.has_value()) {
            j["seed"] = seed.value();
        }

        if (completed_at.has_value()) {
            j["completedAt"] = timeToString(completed_at.value());
        }

        return j;
    }

    ImageGeneration ImageGeneration::fromJson(const nlohmann::json& j) {
        ImageGeneration img;

        if (j.contains("id")) img.id = j["id"].get<int64_t>();
        if (j.contains("userId")) img.user_id = j["userId"].get<int64_t>();
        if (j.contains("requestId")) img.request_id = j["requestId"].get<std::string>();
        if (j.contains("prompt")) img.prompt = j["prompt"].get<std::string>();
        if (j.contains("negativePrompt")) img.negative_prompt = j["negativePrompt"].get<std::string>();
        if (j.contains("numSteps")) img.num_steps = j["numSteps"].get<int>();
        if (j.contains("height")) img.height = j["height"].get<int>();
        if (j.contains("width")) img.width = j["width"].get<int>();
        if (j.contains("seed") && !j["seed"].is_null()) img.seed = j["seed"].get<int>();
        if (j.contains("status")) img.status = j["status"].get<std::string>();
        if (j.contains("imageUrl")) img.image_url = j["imageUrl"].get<std::string>();
        if (j.contains("imageBase64")) img.image_base64 = j["imageBase64"].get<std::string>();
        if (j.contains("errorMessage")) img.error_message = j["errorMessage"].get<std::string>();
        if (j.contains("generationTime")) img.generation_time = j["generationTime"].get<double>();

        return img;
    }

} // namespace models
