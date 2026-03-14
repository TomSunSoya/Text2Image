#pragma once

#include <drogon/HttpController.h>

class ImageController : public drogon::HttpController<ImageController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ImageController::checkHealth, "/api/images/health", drogon::Get);
    ADD_METHOD_TO(ImageController::create, "/api/images", drogon::Post);
    ADD_METHOD_TO(ImageController::listMy, "/api/images/my-list", drogon::Get);
    ADD_METHOD_TO(ImageController::listMyByStatus, "/api/images/my-list/status/{1}", drogon::Get);
    ADD_METHOD_TO(ImageController::getById, "/api/images/{1:[0-9]+}", drogon::Get);
    ADD_METHOD_TO(ImageController::getBinaryById, "/api/images/{1:[0-9]+}/binary", drogon::Get);
    ADD_METHOD_TO(ImageController::getStatusById, "/api/images/{1:[0-9]+}/status", drogon::Get);
    ADD_METHOD_TO(ImageController::deleteById, "/api/images/{1:[0-9]+}", drogon::Delete);
	ADD_METHOD_TO(ImageController::cancelById, "/api/images/{1:[0-9]+}/cancel", drogon::Post);
	ADD_METHOD_TO(ImageController::retryById, "/api/images/{1:[0-9]+}/retry", drogon::Post);
    METHOD_LIST_END

    void checkHealth(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void create(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void listMy(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    void listMyByStatus(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        const std::string& status);

    void getById(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 int64_t id);

    void getBinaryById(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       int64_t id);

    void getStatusById(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       int64_t id);

    void deleteById(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    int64_t id);

    void cancelById(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    int64_t id);

    void retryById(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
		            int64_t id);
};
