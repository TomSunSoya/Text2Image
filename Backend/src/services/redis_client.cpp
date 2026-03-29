#include "redis_client.h"

#include <algorithm>
#include <tuple>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sw/redis++/redis++.h>

struct redis::RedisClient::Impl {
    sw::redis::Redis redis;

    explicit Impl(const RedisConfig& cfg) : redis(buildOpts(cfg), buildPoolOpts(cfg)) {}

private:
    static sw::redis::ConnectionOptions buildOpts(const RedisConfig& cfg) {
        sw::redis::ConnectionOptions opts;
        opts.host = cfg.host;
        opts.port = cfg.port;
        if (!cfg.password.empty())
            opts.password = cfg.password;
        opts.db = cfg.db;
        opts.connect_timeout = std::chrono::milliseconds(cfg.connect_timeout_ms);
        opts.socket_timeout = std::chrono::milliseconds(cfg.socket_timeout_ms);
        return opts;
    }

    static sw::redis::ConnectionPoolOptions buildPoolOpts(const RedisConfig& cfg) {
        sw::redis::ConnectionPoolOptions opts;
        opts.size = static_cast<std::size_t>((std::max)(1, cfg.pool_size));
        return opts;
    }
};

namespace redis {

bool redis::RedisClient::isAvailable() const {
    return config_.enabled && impl_;
}

bool redis::RedisClient::ping() {
    try {
        if (!isAvailable())
            return false;
        impl_->redis.ping();
        return true;
    } catch (const sw::redis::Error& e) {
        spdlog::warn("Redis ping failed: {}", e.what());
        // Honor the startup fallback path for the lifetime of this process.
        impl_.reset();
        config_.enabled = false;
        return false;
    }
}

void redis::RedisClient::enqueueTask(int64_t taskId) {
    try {
        impl_->redis.lpush(config_.task_queue_key, std::to_string(taskId));
    } catch (sw::redis::Error& e) {
        spdlog::warn("Redis enqueueTask failed: {}", e.what());
        throw;      // down to sql
    }
}

void redis::RedisClient::rebuildTaskQueue(const std::vector<int64_t>& taskIds) {
    static const std::string script = R"(
        redis.call('DEL', KEYS[1])
        for i = 1, #ARGV do
            redis.call('LPUSH', KEYS[1], ARGV[i])
        end
        return #ARGV
    )";

    std::vector<std::string> args;
    args.reserve(taskIds.size());
    for (const auto taskId : taskIds) {
        args.push_back(std::to_string(taskId));
    }

    const std::vector<std::string> keys{config_.task_queue_key};

    try {
        std::ignore = impl_->redis.eval<long long>(
            script, keys.begin(), keys.end(), args.begin(), args.end());
    } catch (const sw::redis::Error& e) {
        spdlog::warn("Redis rebuildTaskQueue failed: {}", e.what());
        throw;
    }
}

std::optional<int64_t> redis::RedisClient::dequeueTask(std::chrono::seconds timeout) {
    try {
        auto result = impl_->redis.brpop(config_.task_queue_key, timeout);
        if (!result)
            return std::nullopt;
        return std::stoll(result->second);
    } catch (const sw::redis::TimeoutError& e) {
        spdlog::debug("Redis dequeueTask socket timeout while waiting for work: {}", e.what());
        return std::nullopt;
    } catch (const sw::redis::Error& e) {
        spdlog::warn("Redis dequeueTask failed: {}", e.what());
        return std::nullopt;
    }
}

bool redis::RedisClient::removeFromQueue(int64_t taskId) const {
    try {
        return impl_->redis.lrem(config_.task_queue_key, 0, std::to_string(taskId)) > 0;
    } catch (const sw::redis::Error& e) {
        spdlog::warn("Redis removeFromQueue failed: {}", e.what());
        return false;
    }
}

bool redis::RedisClient::acquireLease(int64_t taskId, const std::string& workerId,
                                      long leaseSeconds) {
    try {
        // set key value NX EX seconds
        return impl_->redis.set(leaseKey(taskId), workerId, std::chrono::seconds(leaseSeconds),
                                sw::redis::UpdateType::NOT_EXIST);
    } catch (const sw::redis::Error& e) {
        spdlog::warn("Redis acquireLease failed: {}", e.what());
        return false;
    }
}

bool redis::RedisClient::renewLease(int64_t taskId, const std::string& workerId,
                                    long leaseSeconds) {
    static const std::string script = R"(
          if redis.call('GET', KEYS[1]) == ARGV[1] then
              redis.call('EXPIRE', KEYS[1], ARGV[2])
              return 1
          end
          return 0
      )";
    try {
        const auto result = impl_->redis.eval<long long>(script, {leaseKey(taskId)},
                                                   {workerId, std::to_string(leaseSeconds)});
        return result == 1;
    } catch (sw::redis::Error &e) {
        spdlog::warn("Redis renewLease failed: {}", e.what());
        return false;
    }
}

bool redis::RedisClient::releaseLease(int64_t taskId, const std::string& workerId) {
    static const std::string script = R"(
          if redis.call('GET', KEYS[1]) == ARGV[1] then
              return redis.call('DEL', KEYS[1])
          end
          return 0
      )";
    try {
        const auto result = impl_->redis.eval<long long>(script, {leaseKey(taskId)}, {workerId});
        return result == 1;
    } catch (sw::redis::Error& e) {
        spdlog::warn("Redis releaseLease failed: {}", e.what());
        return false;
    }

}

bool redis::RedisClient::forceReleaseLease(int64_t taskId) {
    try {
        return impl_->redis.del(leaseKey(taskId)) > 0;
    } catch (sw::redis::Error &e) {
        spdlog::warn("Redis forceReleaseLease failed: {}", e.what());
        return false;
    }
}

bool redis::RedisClient::leaseExists(int64_t taskId) {
    try {
        return impl_->redis.exists(leaseKey(taskId)) > 0;
    } catch (const sw::redis::Error& e) {
        spdlog::warn("Redis leaseExists failed: {}", e.what());
        return false;
    }
}

redis::RedisClient::~RedisClient() = default;

std::string redis::RedisClient::leaseKey(int64_t taskId) const {
    return config_.lease_key_prefix + std::to_string(taskId);
}

RedisConfig parseRedisConfig(const nlohmann::json& j) {
    RedisConfig c;
    c.host = j.value("host", c.host);
    c.port = j.value("port", c.port);
    c.password = j.value("password", c.password);
    c.db = j.value("db", c.db);
    c.pool_size = j.value("pool_size", c.pool_size);
    c.connect_timeout_ms = j.value("connect_timeout_ms", c.connect_timeout_ms);
    c.socket_timeout_ms = j.value("socket_timeout_ms", c.socket_timeout_ms);
    c.task_queue_key = j.value("task_queue_key", c.task_queue_key);
    c.lease_key_prefix = j.value("lease_key_prefix", c.lease_key_prefix);
    c.enabled = j.value("enabled", c.enabled);
    return c;
}

void RedisClient::init(const RedisConfig& cfg) {
    auto& inst = instance();
    inst.config_ = cfg;
    inst.impl_ = std::make_unique<Impl>(cfg);
}

RedisClient& RedisClient::instance() {
    static RedisClient client;
    return client;
}

} // namespace redis
