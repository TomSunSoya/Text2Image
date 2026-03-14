#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

#include "Backend.h"
#include "db_manager.h"
#include "image_service.h"

int main()
{
    try {
        const auto config = backend::loadConfig();
        const auto& serverConfig = config.at("server");
        const auto& dbConfig = config.at("database");

        database::MysqlConfig mysqlConfig;
        mysqlConfig.host = dbConfig.value("host", std::string("127.0.0.1"));
        mysqlConfig.port = dbConfig.value("port", 33060);
        mysqlConfig.user = dbConfig.value("username", std::string());
        mysqlConfig.password = dbConfig.value("password", std::string());
        mysqlConfig.database = dbConfig.value("database", std::string());

        try {
            database::DBManager::init(mysqlConfig);
			ImageService::bootstrapWorkers();
            spdlog::info("Database initialized: {}:{}", mysqlConfig.host, mysqlConfig.port);
        } catch (const std::exception& e) {
            spdlog::warn("Database initialization failed: {}", e.what());
        }

        const auto healthHandler =
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                resp->setBody(R"({"status":"ok"})");
                callback(resp);
            };

        drogon::app().registerHandler("/health", healthHandler);

        const auto host = serverConfig.value("host", std::string("0.0.0.0"));
        const auto port = serverConfig.value("port", 8080);
        const auto threads = serverConfig.value("threads", 1);

        drogon::app()
            .addListener(host, static_cast<uint16_t>(port))
            .setThreadNum(threads);

        spdlog::info("Backend listening on {}:{}", host, port);
        drogon::app().run();
        return 0;
    } catch (const std::exception& e) {
        spdlog::critical("Fatal startup error: {}", e.what());
        return 1;
    }
}
