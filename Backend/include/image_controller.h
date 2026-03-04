#pragma once

#include <drogon/HttpController.h>

class ImageController : public drogon::HttpController<ImageController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ImageController::create, "/api/images", drogon::Post);
    METHOD_LIST_END

    void create(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

