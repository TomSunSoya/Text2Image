#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <drogon/WebSocketConnection.h>

#include "image_generation.h"

class TaskEventHub {
  public:
    static TaskEventHub& instance();

    void subscribe(int64_t userId, const drogon::WebSocketConnectionPtr& connection);
    void unsubscribe(const drogon::WebSocketConnectionPtr& connection);
    void publishTaskUpdated(const models::ImageGeneration& generation);

  private:
    using ConnectionKey = drogon::WebSocketConnection*;
    using ConnectionMap = std::unordered_map<ConnectionKey, drogon::WebSocketConnectionPtr>;

    std::vector<drogon::WebSocketConnectionPtr> copySubscribersLocked(int64_t userId) const;

    mutable std::mutex mutex_;
    std::unordered_map<int64_t, ConnectionMap> subscribers_;
    std::unordered_map<ConnectionKey, int64_t> owners_;
};
