#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "image_generation.h"

class AsyncImageQueue {
public:
    using TaskHandler = std::function<void(const models::ImageGeneration&)>;

    static AsyncImageQueue& instance();

    void start(TaskHandler handler, std::size_t workerCount = 1);
    void enqueue(const models::ImageGeneration& generation);
    std::size_t pendingCount() const;

    ~AsyncImageQueue();

    AsyncImageQueue(const AsyncImageQueue&) = delete;
    AsyncImageQueue& operator=(const AsyncImageQueue&) = delete;

private:
    AsyncImageQueue() = default;

    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<models::ImageGeneration> queue_;
    std::vector<std::thread> workers_;
    TaskHandler handler_;
    bool started_{false};
    bool stopping_{false};
};