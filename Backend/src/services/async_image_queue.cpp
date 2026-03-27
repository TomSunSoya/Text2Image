#include "async_image_queue.h"

#include <algorithm>
#include <exception>
#include <stdexcept>

#include <spdlog/spdlog.h>

AsyncImageQueue& AsyncImageQueue::instance() {
    static AsyncImageQueue queue;
    return queue;
}

void AsyncImageQueue::start(TaskHandler handler, std::size_t workerCount) {
    std::lock_guard lock(mutex_);
    if (started_) {
        return;
    }

    handler_ = std::move(handler);
    if (!handler_) {
        throw std::invalid_argument("AsyncImageQueue handler must not be empty");
    }

    const std::size_t safeWorkerCount = std::max(workerCount, static_cast<std::size_t>(1));
    workers_.reserve(safeWorkerCount);
    for (std::size_t i = 0; i < safeWorkerCount; ++i) {
        workers_.emplace_back([this](std::stop_token st) { workerLoop(std::move(st)); });
    }

    started_ = true;
}

void AsyncImageQueue::enqueue(const models::ImageGeneration& generation) {
    {
        std::lock_guard lock(mutex_);
        if (!started_) {
            throw std::runtime_error("AsyncImageQueue is not running");
        }

        queue_.push(generation);
    }

    cv_.notify_one();
}

std::size_t AsyncImageQueue::pendingCount() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
}

void AsyncImageQueue::workerLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        models::ImageGeneration task;

        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, stopToken, [this] { return !queue_.empty(); });

            if (stopToken.stop_requested() && queue_.empty()) {
                return;
            }

            task = std::move(queue_.front());
            queue_.pop();
        }

        try {
            handler_(task);
        } catch (const std::exception& ex) {
            spdlog::error("AsyncImageQueue worker unhandled exception: {}", ex.what());
        } catch (...) {
            spdlog::error("AsyncImageQueue worker unhandled unknown exception");
        }
    }
}
