#pragma once

#include <drogon/WebSocketController.h>

class TaskWsController : public drogon::WebSocketController<TaskWsController> {
  public:
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/ws/images");
    WS_PATH_LIST_END

    void handleNewMessage(const drogon::WebSocketConnectionPtr& connection, std::string&& message,
                          const drogon::WebSocketMessageType& type) override;

    void handleNewConnection(const drogon::HttpRequestPtr& request,
                             const drogon::WebSocketConnectionPtr& connection) override;

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) override;
};
