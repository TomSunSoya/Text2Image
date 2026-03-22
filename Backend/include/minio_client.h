#pragma once

#include <optional>
#include <string>

class MinioClient {
public:
    struct Config {
        std::string endpoint;
        std::string access_key;
        std::string secret_key;
        std::string bucket;
        std::string region = "us-east-1";
        int presign_expiry_seconds = 3600;
    };

    explicit MinioClient(Config config);

    bool putObject(const std::string& key, const std::string& data,
                   const std::string& contentType = "image/png") const;

    bool deleteObject(const std::string& key) const;

    std::optional<std::string> getObject(const std::string& key) const;

    std::string presignGetUrl(const std::string& key, int expirySeconds = 0) const;

    bool ensureBucketExists() const;

    const Config& config() const { return config_; }

private:
    Config config_;
};
