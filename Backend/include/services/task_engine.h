#pragma once

#include <cstdint>
#include <memory>

#include "services/cache_client.h"
class TaskEngine {
  public:
    TaskEngine();
    ~TaskEngine();

    TaskEngine(const TaskEngine&) = delete;
    TaskEngine& operator=(const TaskEngine&) = delete;
    TaskEngine(TaskEngine&&) = delete;
    TaskEngine& operator=(TaskEngine&&) = delete;

    void bootstrap(std::shared_ptr<cache::ICacheClient> cache = nullptr);
    void enqueue(int64_t taskId);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
