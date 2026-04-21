#include "services/task_engine.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "Backend.h"
#include "database/ImageRepo.h"
#include "services/generation_client.h"
#include "services/redis_client.h"
#include "services/task_event_hub.h"

namespace {

struct TaskEngineConfig {
    int workers{1};
    int poll_interval_ms{500};
    long lease_seconds{900};
    int max_retries{3};
    std::string worker_prefix{"backend-worker"};
};

TaskEngineConfig loadTaskEngineConfig() {
    TaskEngineConfig config;

    try {
        const auto& backendConfig = backend::cachedConfig();
        if (backendConfig.contains("task_engine") && backendConfig.at("task_engine").is_object()) {
            const auto& taskEngineConfig = backendConfig.at("task_engine");
            config.workers = (std::max)(0, taskEngineConfig.value("workers", config.workers));
            config.poll_interval_ms =
                (std::max)(100,
                           taskEngineConfig.value("poll_interval_ms", config.poll_interval_ms));
            config.lease_seconds =
                (std::max)(30l, taskEngineConfig.value("lease_seconds", config.lease_seconds));
            config.max_retries =
                (std::max)(0, taskEngineConfig.value("max_retries", config.max_retries));
            config.worker_prefix = taskEngineConfig.value("worker_prefix", config.worker_prefix);
        }
    } catch (const std::exception& ex) {
        spdlog::error("Failed to load task engine config, using defaults. Exception: {}",
                      ex.what());
    } catch (...) {
        spdlog::error("Failed to load task engine config, using defaults. Unknown exception.");
    }
    return config;
}

std::chrono::seconds leaseRenewInterval(long leaseSeconds) {
    return std::chrono::seconds((std::max)(1l, leaseSeconds / 2));
}

} // namespace

struct TaskEngine::Impl {
    TaskEngineConfig config{loadTaskEngineConfig()};
    std::vector<std::jthread> workers;
    std::once_flag start_once;
    std::mutex notify_mutex;
    std::condition_variable notify_cv;

    void notifyWorkers() {
        notify_cv.notify_all();
    }

    void enqueue(int64_t taskId) {
        bool redisUp = false;
        try {
            auto& r = redis::RedisClient::instance();
            redisUp = r.isAvailable();
            if (redisUp) {
                r.enqueueTask(taskId);
                return;
            }
        } catch (const std::exception& ex) {
            // Redis was reachable but enqueueTask failed: task is in DB Queued but no
            // Redis-blocked worker will see it. Log loudly; periodic recovery in
            // leaseExpiryLoop will re-sync via rebuildTaskQueue.
            spdlog::error("Failed to enqueue task {} to Redis (will recover via lease scanner): {}",
                          taskId, ex.what());
        } catch (...) {
            spdlog::error(
                "Failed to enqueue task {} to Redis (will recover via lease scanner): unknown",
                taskId);
        }
        // Fallback path: wake MySQL-fallback workers waiting on cv.
        notifyWorkers();
    }

    std::jthread startLeaseKeeper(const models::ImageGeneration& task, const std::string& workerId) {
        const auto renewEvery = leaseRenewInterval(config.lease_seconds);

        return std::jthread([taskId = task.id, userId = task.user_id, workerId,
                             leaseSeconds = config.lease_seconds,
                             renewEvery](std::stop_token stopToken) {
            ImageRepo repo;
            auto nextRenewal = std::chrono::steady_clock::now() + renewEvery;

            while (!stopToken.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (stopToken.stop_requested()) {
                    break;
                }

                const auto now = std::chrono::steady_clock::now();
                if (now < nextRenewal) {
                    continue;
                }

                try {
                    auto& r = redis::RedisClient::instance();
                    if (r.isAvailable() && !r.renewLease(taskId, workerId, leaseSeconds)) {
                        spdlog::warn("Redis lease lost id = {}, worker_id = {}", taskId, workerId);
                    }
                } catch (...) {
                    spdlog::info("Redis is unavailable, start MySQL renew");
                }

                try {
                    if (!repo.renewLease(taskId, userId, workerId, leaseSeconds)) {
                        spdlog::warn("lease keeper lost task claim id={}, user_id={}, worker_id={}",
                                     taskId, userId, workerId);
                        break;
                    }
                } catch (const std::exception& ex) {
                    spdlog::warn("lease keeper failed to renew lease id={}, user_id={}, "
                                 "worker_id={}, reason={}",
                                 taskId, userId, workerId, ex.what());
                } catch (...) {
                    spdlog::warn("lease keeper failed to renew lease id={}, user_id={}, "
                                 "worker_id={}, reason=unknown",
                                 taskId, userId, workerId);
                }

                nextRenewal = now + renewEvery;
            }
        });
    }

    void processClaimedTask(ImageRepo& repo, models::ImageGeneration& task,
                            const std::string& workerId) {
        spdlog::info("task worker claimed task id = {}, user_id = {}, request_id = {}, worker_id = {}",
                     task.id, task.user_id, task.request_id, workerId);
        TaskEventHub::instance().publishTaskUpdated(task);

        auto leaseKeeper = startLeaseKeeper(task, workerId);
        auto result = GenerationClient::generate(task);
        const bool finished = repo.finishClaimedTask(result);
        leaseKeeper.request_stop();

        try {
            auto& r = redis::RedisClient::instance();
            if (r.isAvailable()) {
                r.releaseLease(task.id, workerId);
            }
        } catch (...) {
        }

        if (!finished) {
            GenerationClient::cleanupOrphanedStoredImage(result);
            spdlog::warn("task worker failed to finish claimed task id = {}", task.id);
        } else {
            TaskEventHub::instance().publishTaskUpdated(result);
        }
    }

    void workerLoop(std::stop_token stopToken, const std::string& workerId) {
        ImageRepo repo;

        while (!stopToken.stop_requested()) {
            try {
                auto& r = redis::RedisClient::instance();
                const bool redisUp = r.isAvailable();

                if (redisUp) {
                    auto taskId = r.dequeueTask(std::chrono::seconds(1));
                    if (!taskId) {
                        continue;
                    }

                    if (!r.acquireLease(*taskId, workerId, config.lease_seconds)) {
                        spdlog::debug("worker {} lost lease race for task {}", workerId, *taskId);
                        continue;
                    }
                    auto task = repo.claimTaskById(*taskId, workerId, config.lease_seconds);
                    if (!task) {
                        r.releaseLease(*taskId, workerId);
                        continue;
                    }
                    processClaimedTask(repo, *task, workerId);
                } else {
                    auto task = repo.claimNextTask(workerId, config.lease_seconds);
                    if (!task) {
                        std::unique_lock lock(notify_mutex);
                        notify_cv.wait_for(lock,
                                           std::chrono::milliseconds(config.poll_interval_ms));
                        continue;
                    }
                    processClaimedTask(repo, *task, workerId);
                }
            } catch (const std::exception& ex) {
                spdlog::error("task worker exception: {}, worker_id={}", ex.what(), workerId);
                std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
            } catch (...) {
                spdlog::error("task worker unknown exception, worker_id={}", workerId);
                std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
            }
        }
    }

    void leaseExpiryLoop(std::stop_token stopToken, int intervalSeconds) {
        ImageRepo repo;

        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
            if (stopToken.stop_requested()) {
                break;
            }

            try {
                auto recoveredIds = repo.expireLeasesReturningIds();
                if (!recoveredIds.empty()) {
                    spdlog::info("lease expiry scanner recovered {} task(s)", recoveredIds.size());
                    for (auto id : recoveredIds) {
                        enqueue(id);
                    }
                }
            } catch (const std::exception& ex) {
                spdlog::error("lease expiry scanner error: {}", ex.what());
            }

            // Periodic re-sync: recovers tasks whose enqueue() failed mid-flight
            // (Redis up but command threw) by rebuilding the queue from DB.
            recoverOrphanedTasks();
        }
    }

    void recoverOrphanedTasks() {
        try {
            auto& r = redis::RedisClient::instance();
            if (!r.isAvailable()) {
                return;
            }

            ImageRepo repo;
            auto ids = repo.findQueuedTaskIds();
            r.rebuildTaskQueue(ids);

            if (!ids.empty()) {
                spdlog::info("Recovered {} orphaned queued task(s) into Redis", ids.size());
            }
        } catch (const std::exception& ex) {
            spdlog::warn("Failed to recover orphaned tasks: {}", ex.what());
        }
    }

    void bootstrap() {
        std::call_once(start_once, [this] {
            if (config.workers <= 0) {
                spdlog::info("ImageService task engine disabled");
                return;
            }

            recoverOrphanedTasks();

            workers.reserve(static_cast<size_t>(config.workers) + 1);

            for (int i = 0; i < config.workers; ++i) {
                const auto workerId = std::format("{}-{}", config.worker_prefix, i + 1);
                workers.emplace_back([this, workerId](std::stop_token stopToken) {
                    workerLoop(stopToken, workerId);
                });
            }

            constexpr int kLeaseExpiryIntervalSeconds = 30;
            workers.emplace_back([this](std::stop_token stopToken) {
                leaseExpiryLoop(stopToken, kLeaseExpiryIntervalSeconds);
            });

            spdlog::info("ImageService task engine started with {} worker(s) + lease scanner",
                         config.workers);
        });
    }
};

TaskEngine::TaskEngine() : impl_(std::make_unique<Impl>()) {}

TaskEngine::~TaskEngine() = default;

void TaskEngine::bootstrap() {
    impl_->bootstrap();
}

void TaskEngine::enqueue(int64_t taskId) {
    // Lazy bootstrap: callers (ImageService::create / retryById) rely on enqueue
    // alone to make the task progress. Without this, if no one called bootstrap()
    // explicitly the task would sit in the queue forever.
    impl_->bootstrap();
    impl_->enqueue(taskId);
}
