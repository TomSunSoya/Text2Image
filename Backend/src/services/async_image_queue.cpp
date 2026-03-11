#include "async_image_queue.h"

#include <algorithm>
#include <exception>
#include <stdexcept>

#include <spdlog/spdlog.h>

AsyncImageQueue& AsyncImageQueue::instance()
{
    static AsyncImageQueue queue;
    return queue;
}

void AsyncImageQueue::start(TaskHandler handler, std::size_t workerCount)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        return;
    }

    handler_ = std::move(handler);
    if (!handler_) {
        throw std::invalid_argument("AsyncImageQueue handler must not be empty");
    }

    const std::size_t safeWorkerCount = (std::max)(workerCount, static_cast<std::size_t>(1));
    workers_.reserve(safeWorkerCount);
    for (std::size_t i = 0; i < safeWorkerCount; ++i) {
        workers_.emplace_back(&AsyncImageQueue::workerLoop, this);
    }

    started_ = true;
    stopping_ = false;
}

void AsyncImageQueue::enqueue(const models::ImageGeneration& generation)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || stopping_) {
            throw std::runtime_error("AsyncImageQueue is not running");
        }

        queue_.push(generation);
    }

    cv_.notify_one();
}

std::size_t AsyncImageQueue::pendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

AsyncImageQueue::~AsyncImageQueue()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }

    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void AsyncImageQueue::workerLoop()
{
    while (true) {
        models::ImageGeneration task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return stopping_ || !queue_.empty();
            });

            if (stopping_ && queue_.empty()) {
                return;
            }

            task = queue_.front();
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