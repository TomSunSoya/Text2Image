#pragma once

#include <cstdint>
#include <memory>

class TaskEngine {
  public:
    TaskEngine();
    ~TaskEngine();

    TaskEngine(const TaskEngine&) = delete;
    TaskEngine& operator=(const TaskEngine&) = delete;
    TaskEngine(TaskEngine&&) = delete;
    TaskEngine& operator=(TaskEngine&&) = delete;

    void bootstrap();
    void enqueue(int64_t taskId);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
