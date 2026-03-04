#include "image_controller.h"

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "image_service.h"

void ImageController::create(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    using nlohmann::json;

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    try {
        const auto payload = json::parse(req->getBody());

        ImageService service;
        ServiceError error;
        auto result = service.create(payload, error);
        if (!result) {
            resp->setStatusCode(error.status);
            resp->setBody(json{{"error", error.message}}.dump());
            callback(resp);
            return;
        }

        resp->setStatusCode(drogon::k200OK);
        resp->setBody(result->generation.toJson().dump());
        callback(resp);
    } catch (const json::parse_error& e) {
        spdlog::error("ImageController::create parse error: {}", e.what());
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody(R"({"error":"invalid json body"})");
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("ImageController::create error: {}", e.what());
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody(R"({"error":"internal error"})");
        callback(resp);
    }
}
