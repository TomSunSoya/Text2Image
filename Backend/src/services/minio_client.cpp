#include "services/minio_client.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <miniocpp/args.h>
#include <miniocpp/client.h>
#include <miniocpp/http.h>
#include <miniocpp/providers.h>
#include <miniocpp/request.h>
#include <spdlog/spdlog.h>

namespace {

minio::s3::BaseUrl buildBaseUrl(const MinioClient::Config& config) {
    if (config.endpoint.empty()) {
        throw std::runtime_error("MinIO endpoint is empty");
    }

    const auto parsed = minio::http::Url::Parse(config.endpoint);
    if (!parsed) {
        throw std::runtime_error("invalid MinIO endpoint: " + config.endpoint);
    }

    if (!parsed.path.empty() && parsed.path != "/") {
        throw std::runtime_error("MinIO endpoint must not include a path: " + config.endpoint);
    }

    minio::s3::BaseUrl value(parsed.host, parsed.https, config.region);
    if (parsed.port != 0) {
        value.port = parsed.port;
    }
    value.virtual_style = false;
    return value;
}

minio::creds::StaticProvider buildProvider(const MinioClient::Config& config) {
    if (config.access_key.empty()) {
        throw std::runtime_error("MinIO access key is empty");
    }

    if (config.secret_key.empty()) {
        throw std::runtime_error("MinIO secret key is empty");
    }

    return minio::creds::StaticProvider(config.access_key, config.secret_key);
}

std::atomic_uint64_t g_next_client_id{1};

struct ThreadLocalClientEntry {
    minio::s3::BaseUrl base_url;
    minio::creds::StaticProvider provider;
    minio::s3::Client client;

    explicit ThreadLocalClientEntry(const MinioClient::Config& config)
        : base_url(buildBaseUrl(config)), provider(buildProvider(config)),
          client(base_url, &provider) {}
};

std::string describeResponse(const minio::s3::Response& response) {
    if (const auto err = response.Error()) {
        return err.String();
    }

    if (!response.code.empty() || !response.message.empty()) {
        return response.code + (response.message.empty() ? std::string() : ": " + response.message);
    }

    if (response.status_code != 0) {
        return "http " + std::to_string(response.status_code);
    }

    return "unknown error";
}

} // namespace

struct MinioClient::ClientBundle {
    const uint64_t client_id;
    Config config;

    explicit ClientBundle(Config config)
        : client_id(g_next_client_id.fetch_add(1, std::memory_order_relaxed)),
          config(std::move(config)) {}

    minio::s3::Client& client() {
        thread_local std::unordered_map<uint64_t, ThreadLocalClientEntry> clients;
        return clients.try_emplace(client_id, config).first->second.client;
    }
};

MinioClient::MinioClient(Config config) : config_(std::move(config)) {
    while (!config_.endpoint.empty() && config_.endpoint.back() == '/') {
        config_.endpoint.pop_back();
    }

    if (config_.bucket.empty()) {
        throw std::runtime_error("MinIO bucket is empty");
    }

    (void)buildBaseUrl(config_);
    (void)buildProvider(config_);

    bundle_ = std::make_unique<ClientBundle>(config_);
}

MinioClient::~MinioClient() = default;

bool MinioClient::putObject(const std::string& key, const std::string& data,
                            const std::string& contentType) const {
    minio::s3::PutObjectApiArgs args;
    args.bucket = config_.bucket;
    args.object = key;
    args.data = data;
    args.object_size = static_cast<long>(data.size());
    args.content_type = contentType;

    auto response = static_cast<minio::s3::BaseClient&>(bundle_->client()).PutObject(args);
    if (!response) {
        spdlog::error("MinioClient::putObject failed, key={}, reason={}", key,
                      describeResponse(response));
        return false;
    }

    return true;
}

bool MinioClient::deleteObject(const std::string& key) const {
    minio::s3::RemoveObjectArgs args;
    args.bucket = config_.bucket;
    args.object = key;

    auto response = bundle_->client().RemoveObject(args);
    if (!response) {
        spdlog::error("MinioClient::deleteObject failed, key={}, reason={}", key,
                      describeResponse(response));
        return false;
    }

    return true;
}

std::optional<std::string> MinioClient::getObject(const std::string& key) const {
    std::string body;
    minio::s3::GetObjectArgs args;
    args.bucket = config_.bucket;
    args.object = key;
    args.datafunc = [&body](minio::http::DataFunctionArgs cbArgs) {
        body += cbArgs.datachunk;
        return true;
    };

    auto response = bundle_->client().GetObject(args);
    if (!response) {
        spdlog::error("MinioClient::getObject failed, key={}, reason={}", key,
                      describeResponse(response));
        return std::nullopt;
    }

    return body;
}

std::string MinioClient::presignGetUrl(const std::string& key, int expirySeconds) const {
    minio::s3::GetPresignedObjectUrlArgs args;
    args.bucket = config_.bucket;
    args.object = key;
    args.method = minio::http::Method::kGet;
    if (expirySeconds > 0) {
        args.expiry_seconds = static_cast<unsigned int>(expirySeconds);
    } else {
        args.expiry_seconds = static_cast<unsigned int>(config_.presign_expiry_seconds);
    }

    auto response = bundle_->client().GetPresignedObjectUrl(args);
    if (!response) {
        throw std::runtime_error("failed to generate MinIO presigned URL: " +
                                 describeResponse(response));
    }

    return response.url;
}

bool MinioClient::ensureBucketExists() const {
    minio::s3::BucketExistsArgs existsArgs;
    existsArgs.bucket = config_.bucket;
    existsArgs.region = config_.region;

    auto exists = bundle_->client().BucketExists(existsArgs);
    if (!exists) {
        spdlog::error("MinioClient::ensureBucketExists check failed, bucket={}, reason={}",
                      config_.bucket, describeResponse(exists));
        return false;
    }

    if (exists.exist) {
        spdlog::info("MinIO bucket '{}' ready", config_.bucket);
        return true;
    }

    minio::s3::MakeBucketArgs makeArgs;
    makeArgs.bucket = config_.bucket;
    makeArgs.region = config_.region;

    auto created = bundle_->client().MakeBucket(makeArgs);
    if (!created) {
        spdlog::error("MinioClient::ensureBucketExists create failed, bucket={}, reason={}",
                      config_.bucket, describeResponse(created));
        return false;
    }

    spdlog::info("MinIO bucket '{}' created", config_.bucket);
    return true;
}
