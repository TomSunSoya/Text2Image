#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <vector>

#include "models/image_generation.h"

class AsyncImageQueue {
  public:
    using TaskHandler = std::function<void(const models::ImageGeneration&)>;

    static AsyncImageQueue& instance();

    void start(TaskHandler handler, std::size_t workerCount = 1);
    void enqueue(const models::ImageGeneration& generation);
    std::size_t pendingCount() const;

    ~AsyncImageQueue() = default;

    AsyncImageQueue(const AsyncImageQueue&) = delete;
    AsyncImageQueue& operator=(const AsyncImageQueue&) = delete;

  private:
    AsyncImageQueue() = default;

    void workerLoop(std::stop_token stopToken);

    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    std::queue<models::ImageGeneration> queue_;
    std::vector<std::jthread> workers_;
    TaskHandler handler_;
    bool started_{false};
};
