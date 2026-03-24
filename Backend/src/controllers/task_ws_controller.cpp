#include "task_ws_controller.h"

#include <nlohmann/json.hpp>

#include "jwt_utils.h"
#include "task_event_hub.h"

void TaskWsController::handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                                        std::string&&, const drogon::WebSocketMessageType& type) {
    if (type == drogon::WebSocketMessageType::Ping) {
        connection->send("", drogon::WebSocketMessageType::Pong);
    }
}

void TaskWsController::handleNewConnection(const drogon::HttpRequestPtr& request,
                                           const drogon::WebSocketConnectionPtr& connection) {
    const auto token = request->getParameter("token");
    const auto payload = utils::verifyToken(token);
    if (!payload || payload->user_id <= 0) {
        connection->shutdown(drogon::CloseCode::kViolation, "invalid token");
        return;
    }

    TaskEventHub::instance().subscribe(payload->user_id, connection);

    const nlohmann::json ready = {{"type", "image.task.socket.ready"}};
    connection->send(ready.dump());
}

void TaskWsController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) {
    TaskEventHub::instance().unsubscribe(connection);
}
