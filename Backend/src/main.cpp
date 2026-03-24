#include <algorithm>
#include <string>
#include <vector>

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

#include "Backend.h"
#include "db_manager.h"
#include "image_service.h"
#include "minio_client.h"

int main() {
    try {
        const auto config = backend::loadConfig();
        const auto& serverConfig = config.at("server");
        const auto& dbConfig = config.at("database");

        // --- JWT secret validation ---
        {
            const auto jwtSecret = config.at("jwt").value("secret", std::string());
            const std::vector<std::string> insecureSecrets = {
                "", "CHANGE_ME", "change-me-via-JWT_SECRET", "development-secret-change-me"};
            for (const auto& insecure : insecureSecrets) {
                if (jwtSecret == insecure) {
                    spdlog::critical("JWT secret is not configured! "
                                     "Set JWT_SECRET env var or update jwt.secret in config.json");
                    return 1;
                }
            }
        }

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

        // --- MinIO initialization ---
        try {
            const auto& minioConfig = config.at("minio");
            MinioClient::Config minioCfg;
            minioCfg.endpoint = minioConfig.value("endpoint", std::string("http://localhost:9000"));
            minioCfg.access_key = minioConfig.value("access_key", std::string());
            minioCfg.secret_key = minioConfig.value("secret_key", std::string());
            minioCfg.bucket = minioConfig.value("bucket", std::string("zimage"));
            minioCfg.region = minioConfig.value("region", std::string("us-east-1"));

            MinioClient minio(minioCfg);
            if (minio.ensureBucketExists()) {
                spdlog::info("MinIO ready: {}/{}", minioCfg.endpoint, minioCfg.bucket);
            } else {
                spdlog::warn("MinIO bucket creation failed — image storage may not work");
            }
        } catch (const std::exception& e) {
            spdlog::warn("MinIO initialization failed: {}", e.what());
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

        // Limit request body to 1MB to prevent memory exhaustion from
        // oversized payloads (the create endpoint only needs prompt text).
        constexpr size_t kMaxBodySize = 1 * 1024 * 1024;

        // --- CORS setup ---
        bool corsAllowAll = false;
        std::vector<std::string> corsOrigins;
        std::string corsAllowMethods = "GET, POST, PUT, DELETE, OPTIONS";
        std::string corsAllowHeaders = "Content-Type, Authorization";

        if (config.contains("cors") && config.at("cors").value("enabled", false)) {
            const auto& corsConfig = config.at("cors");

            auto origins = corsConfig.value("allow_origins", std::vector<std::string>{});
            for (const auto& origin : origins) {
                if (origin == "*") {
                    corsAllowAll = true;
                }
                corsOrigins.push_back(origin);
            }

            auto methods =
                corsConfig.value("allow_methods", std::vector<std::string>{"GET", "POST", "PUT",
                                                                           "DELETE", "OPTIONS"});
            corsAllowMethods.clear();
            for (size_t i = 0; i < methods.size(); ++i) {
                if (i > 0)
                    corsAllowMethods += ", ";
                corsAllowMethods += methods[i];
            }

            auto headers = corsConfig.value(
                "allow_headers", std::vector<std::string>{"Content-Type", "Authorization"});
            corsAllowHeaders.clear();
            for (size_t i = 0; i < headers.size(); ++i) {
                if (i > 0)
                    corsAllowHeaders += ", ";
                corsAllowHeaders += headers[i];
            }

            if (corsAllowAll) {
                spdlog::warn("CORS allow_origins contains '*' — all origins accepted. "
                             "Set specific origins for production.");
            }

            // Handle OPTIONS preflight
            drogon::app().registerPreRoutingAdvice(
                [corsOrigins, corsAllowAll, corsAllowMethods,
                 corsAllowHeaders](const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& acb,
                                   std::function<void()>&& accb) {
                    if (req->method() != drogon::Options) {
                        accb();
                        return;
                    }

                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k204NoContent);

                    const auto& origin = req->getHeader("Origin");
                    if (!origin.empty() &&
                        (corsAllowAll || std::find(corsOrigins.begin(), corsOrigins.end(),
                                                   origin) != corsOrigins.end())) {
                        resp->addHeader("Access-Control-Allow-Origin", corsAllowAll ? "*" : origin);
                        resp->addHeader("Access-Control-Allow-Methods", corsAllowMethods);
                        resp->addHeader("Access-Control-Allow-Headers", corsAllowHeaders);
                        resp->addHeader("Access-Control-Max-Age", "86400");
                    }

                    acb(resp);
                });

            // Add CORS headers to all responses, including framework-generated
            // ones such as 404 responses.
            drogon::app().registerPreSendingAdvice(
                [corsOrigins, corsAllowAll, corsAllowMethods, corsAllowHeaders](
                    const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
                    const auto& origin = req->getHeader("Origin");
                    if (origin.empty()) {
                        return;
                    }

                    if (corsAllowAll || std::find(corsOrigins.begin(), corsOrigins.end(), origin) !=
                                            corsOrigins.end()) {
                        resp->addHeader("Access-Control-Allow-Origin", corsAllowAll ? "*" : origin);
                        resp->addHeader("Access-Control-Allow-Methods", corsAllowMethods);
                        resp->addHeader("Access-Control-Allow-Headers", corsAllowHeaders);
                        if (!corsAllowAll) {
                            resp->addHeader("Vary", "Origin");
                        }
                    }
                });

            spdlog::info("CORS enabled for {} origin(s)", corsOrigins.size());
        }

        drogon::app()
            .addListener(host, static_cast<uint16_t>(port))
            .setThreadNum(threads)
            .setClientMaxBodySize(kMaxBodySize);

        spdlog::info("Backend listening on {}:{}", host, port);
        drogon::app().run();
        return 0;
    } catch (const std::exception& e) {
        spdlog::critical("Fatal startup error: {}", e.what());
        return 1;
    }
}
