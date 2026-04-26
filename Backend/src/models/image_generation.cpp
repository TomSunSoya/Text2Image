#include "models/image_generation.h"
#include "utils/chrono_utils.h"

namespace models {

static void putOptionalTime(nlohmann::json& j, const char* key,
                            const std::optional<std::chrono::system_clock::time_point>& value) {
    if (value.has_value()) {
        j[key] = utils::chrono::toDbString(*value);
    }
}

static std::optional<std::string> readStringAny(const nlohmann::json& j,
                                                std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (j.contains(key) && j[key].is_string()) {
            return j[key].get<std::string>();
        }
    }

    return std::nullopt;
}

static std::optional<int> readIntAny(const nlohmann::json& j,
                                     std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (j.contains(key) && j[key].is_number_integer())
            return j[key].get<int>();
    }

    return std::nullopt;
}

static std::optional<int64_t> readInt64_tAny(const nlohmann::json& j,
                                             std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (j.contains(key) && j[key].is_number_integer())
            return j[key].get<int64_t>();
    }

    return std::nullopt;
}

static std::optional<double> readDoubleAny(const nlohmann::json& j,
                                           std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (j.contains(key) && j[key].is_number())
            return j[key].get<double>();
    }

    return std::nullopt;
}

static std::optional<std::chrono::system_clock::time_point>
readTimeAny(const nlohmann::json& j, std::initializer_list<const char*> keys) {
    if (const auto value = readStringAny(j, keys)) {
        return utils::chrono::fromDbString(*value);
    }

    return std::nullopt;
}

nlohmann::json ImageGeneration::toJson() const {
    nlohmann::json j = {{"id", id},
                        {"taskId", id},
                        {"userId", user_id},
                        {"requestId", request_id},
                        {"prompt", prompt},
                        {"negativePrompt", negative_prompt},
                        {"numSteps", num_steps},
                        {"height", height},
                        {"width", width},
                        {"status", std::string(statusToString(status))},
                        {"retryCount", retry_count},
                        {"maxRetries", max_retries},
                        {"failureCode", failure_code},
                        {"workerId", worker_id},
                        {"imageUrl", image_url},
                        {"errorMessage", error_message},
                        {"generationTime", generation_time},
                        {"createdAt", utils::chrono::toDbString(created_at)},
                        {"thumbnailUrl", thumbnail_url},
                        {"storageKey", storage_key},
                        {"imageBytes", image_bytes}};

    if (seed.has_value()) {
        j["seed"] = seed.value();
    }

    putOptionalTime(j, "startedAt", started_at);
    putOptionalTime(j, "completedAt", completed_at);
    putOptionalTime(j, "cancelledAt", cancelled_at);
    putOptionalTime(j, "leaseExpiresAt", lease_expires_at);

    return j;
}

ImageGeneration ImageGeneration::fromJson(const nlohmann::json& j) {
    ImageGeneration img;

    if (const auto value = readInt64_tAny(j, {"id", "taskId"}))
        img.id = *value;
    if (const auto value = readInt64_tAny(j, {"user_id", "userId"}))
        img.user_id = *value;
    if (const auto value = readStringAny(j, {"request_id", "requestId"}))
        img.request_id = *value;
    if (const auto value = readStringAny(j, {"prompt"}))
        img.prompt = *value;
    if (const auto value = readStringAny(j, {"negative_prompt", "negativePrompt"}))
        img.negative_prompt = *value;
    if (const auto value = readIntAny(j, {"num_steps", "numSteps"}))
        img.num_steps = *value;
    if (const auto value = readIntAny(j, {"height"}))
        img.height = *value;
    if (const auto value = readIntAny(j, {"width"}))
        img.width = *value;
    if (const auto value = readIntAny(j, {"seed"}))
        img.seed = *value;
    if (const auto value = readStringAny(j, {"status"}))
        img.status = statusFromString(*value);
    if (const auto value = readStringAny(j, {"image_url", "imageUrl"}))
        img.image_url = *value;
    if (const auto value = readStringAny(j, {"thumbnail_url", "thumbnailUrl"}))
        img.thumbnail_url = *value;
    if (const auto value = readStringAny(j, {"storage_key", "storage_key", "storageKey"}))
        img.storage_key = *value;
    if (const auto value = readStringAny(j, {"image_bytes", "imageBytes"}))
        img.image_bytes = *value;
    if (const auto value = readStringAny(j, {"error_message", "errorMessage"}))
        img.error_message = *value;
    if (const auto value = readDoubleAny(j, {"generation_time", "generationTime"}))
        img.generation_time = *value;
    if (const auto value = readIntAny(j, {"retry_count", "retryCount"}))
        img.retry_count = *value;
    if (const auto value = readIntAny(j, {"max_retries", "maxRetries"}))
        img.max_retries = *value;
    if (const auto value = readStringAny(j, {"failure_code", "failureCode"}))
        img.failure_code = *value;
    if (const auto value = readStringAny(j, {"worker_id", "workerId"}))
        img.worker_id = *value;
    if (const auto value = readTimeAny(j, {"created_at", "createdAt"}))
        img.created_at = *value;
    if (const auto value = readTimeAny(j, {"started_at", "startedAt"}))
        img.started_at = *value;
    if (const auto value = readTimeAny(j, {"completed_at", "completedAt"}))
        img.completed_at = *value;
    if (const auto value = readTimeAny(j, {"cancelled_at", "cancelledAt"}))
        img.cancelled_at = *value;
    if (const auto value = readTimeAny(j, {"lease_expires_at", "leaseExpiresAt"}))
        img.lease_expires_at = *value;

    return img;
}

} // namespace models
