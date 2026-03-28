#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace redis {

struct RedisConfig {
    std::string host{"127.0.0.1"};
    int port{6379};
    std::string password;
    int db{0};
    int pool_size{4};
    int connect_timeout_ms{1000};
    int socket_timeout_ms{5000};
    std::string task_queue_key{"zimage:task_queue"};
    std::string lease_key_prefix{"zimage:lease:"};
    bool enabled{true};
};

RedisConfig parseRedisConfig(const nlohmann::json& j);

class RedisClient {
  public:
    static void init(const RedisConfig& cfg);
    static RedisClient& instance();

    bool isAvailable() const;
    bool ping();

    // job queue
    void enqueueTask(int64_t taskId);
    std::optional<int64_t> dequeueTask(std::chrono::seconds timeout);
    bool removeFromQueue(int64_t taskId) const;
    void rebuildTaskQueue(const std::vector<int64_t>& taskIds);

    // distribute lease
    bool acquireLease(int64_t taskId, const std::string& workerId, long leaseSeconds);
    bool renewLease(int64_t taskId, const std::string& workerId, long leaseSeconds);
    bool releaseLease(int64_t taskId, const std::string& workerId);
    bool forceReleaseLease(int64_t taskId);
    bool leaseExists(int64_t taskId);

    ~RedisClient();
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

private:
    RedisClient() = default;
    std::string leaseKey(int64_t taskId) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    RedisConfig config_;
};

} // namespace redis
