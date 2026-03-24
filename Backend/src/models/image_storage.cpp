#include "image_storage.h"

#include <cctype>
#include <format>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include "Backend.h"
#include "minio_client.h"

namespace {

const MinioClient& minioClient() {
    static const MinioClient client = [] {
        MinioClient::Config cfg;
        try {
            const auto& config = backend::cachedConfig();
            if (config.contains("minio") && config.at("minio").is_object()) {
                const auto& m = config.at("minio");
                cfg.endpoint = m.value("endpoint", cfg.endpoint);
                cfg.access_key = m.value("access_key", cfg.access_key);
                cfg.secret_key = m.value("secret_key", cfg.secret_key);
                cfg.bucket = m.value("bucket", cfg.bucket);
                cfg.region = m.value("region", cfg.region);
                cfg.presign_expiry_seconds =
                    m.value("presign_expiry_seconds", cfg.presign_expiry_seconds);
            }
        } catch (const std::exception& ex) {
            spdlog::error("Failed to load MinIO config: {}", ex.what());
        }
        return MinioClient(cfg);
    }();
    return client;
}

std::string sanitizeKeyPart(std::string value) {
    for (auto& ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || ch == '-' || ch == '_')) {
            ch = '_';
        }
    }

    if (value.empty())
        value = "unknown";

    return value;
}

} // namespace

StoredImage ImageStorage::store(int64_t userId, const std::string& requestId,
                                const std::string& rawBytes, const std::string& contentType) const {
    if (userId <= 0)
        throw std::runtime_error("userId must be positive");

    if (rawBytes.empty())
        throw std::runtime_error("rawBytes is empty");

    const auto safeRequestId = sanitizeKeyPart(requestId);

    std::string ext = "png";
    if (contentType.find("jpeg") != std::string::npos ||
        contentType.find("jpg") != std::string::npos) {
        ext = "jpg";
    } else if (contentType.find("webp") != std::string::npos) {
        ext = "webp";
    }

    const auto objectKey = std::format("images/{}/{}.{}", userId, safeRequestId, ext);

    if (!minioClient().putObject(objectKey, rawBytes, contentType)) {
        throw std::runtime_error("failed to upload image to MinIO: " + objectKey);
    }

    spdlog::info("ImageStorage::store uploaded key={}, size={}", objectKey, rawBytes.size());

    StoredImage stored;
    stored.storage_key = objectKey;
    stored.content_type = contentType;
    return stored;
}

std::optional<std::string> ImageStorage::getBytes(const std::string& storageKey) const {
    if (storageKey.empty()) {
        return std::nullopt;
    }

    return minioClient().getObject(storageKey);
}

std::string ImageStorage::presignUrl(const std::string& storageKey, int expirySeconds) const {
    return minioClient().presignGetUrl(storageKey, expirySeconds);
}

bool ImageStorage::remove(const std::string& storageKey) const {
    if (storageKey.empty()) {
        return false;
    }

    return minioClient().deleteObject(storageKey);
}

std::string ImageStorage::contentTypeForKey(const std::string& storageKey) const {
    if (storageKey.ends_with(".jpg") || storageKey.ends_with(".jpeg"))
        return "image/jpeg";
    if (storageKey.ends_with(".webp"))
        return "image/webp";
    return "image/png";
}
