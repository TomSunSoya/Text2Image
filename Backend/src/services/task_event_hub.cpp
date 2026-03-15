#include "task_event_hub.h"

#include <nlohmann/json.hpp>

TaskEventHub& TaskEventHub::instance()
{
    static TaskEventHub hub;
    return hub;
}

void TaskEventHub::subscribe(int64_t userId, const drogon::WebSocketConnectionPtr& connection)
{
    if (!connection || userId <= 0) {
        return;
    }

    std::lock_guard lock(mutex_);
    auto* key = connection.get();
    subscribers_[userId][key] = connection;
    owners_[key] = userId;
}

void TaskEventHub::unsubscribe(const drogon::WebSocketConnectionPtr& connection)
{
    if (!connection) {
        return;
    }

    std::lock_guard lock(mutex_);
    auto* key = connection.get();
    const auto ownerIt = owners_.find(key);
    if (ownerIt == owners_.end()) {
        return;
    }

    const auto userId = ownerIt->second;
    owners_.erase(ownerIt);

    const auto subscribersIt = subscribers_.find(userId);
    if (subscribersIt == subscribers_.end()) {
        return;
    }

    subscribersIt->second.erase(key);
    if (subscribersIt->second.empty()) {
        subscribers_.erase(subscribersIt);
    }
}

void TaskEventHub::publishTaskUpdated(const models::ImageGeneration& generation)
{
    if (generation.user_id <= 0) {
        return;
    }

    const auto subscribers = copySubscribersLocked(generation.user_id);
    if (subscribers.empty()) {
        return;
    }

    const nlohmann::json payload = {
        {"type", "image.task.updated"},
        {"task", generation.toJson(false)}
    };
    const auto serialized = payload.dump();

    for (const auto& connection : subscribers) {
        if (connection) {
            try {
                connection->send(serialized);
            } catch (...) {
            }
        }
    }
}

std::vector<drogon::WebSocketConnectionPtr> TaskEventHub::copySubscribersLocked(int64_t userId) const
{
    std::lock_guard lock(mutex_);
    std::vector<drogon::WebSocketConnectionPtr> subscribers;

    const auto it = subscribers_.find(userId);
    if (it == subscribers_.end()) {
        return subscribers;
    }

    subscribers.reserve(it->second.size());
    for (const auto& [_, connection] : it->second) {
        if (connection) {
            subscribers.push_back(connection);
        }
    }

    return subscribers;
}
