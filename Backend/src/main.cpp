#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

#include "Backend.h"
#include "database/db_manager.h"
#include "services/cache_client.h"
#include "services/image_service.h"
#include "services/minio_client.h"
#include "services/null_cache_client.h"
#include "services/rate_limiter.h"
#include "services/redis_client.h"
#include "services/image_cache_key.h"
#include "controllers/metrics_controller.h"
#include "services/metrics_cache_client.h"
#include "services/metrics_registry.h"
#include "utils/request_id.h"

namespace {

std::string toLowerCopy(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool isProductionEnvironment() {
    auto env = []() -> std::optional<std::string> {
#ifdef _WIN32
        char* raw = nullptr;
        size_t size = 0;
        if (_dupenv_s(&raw, &size, "ENV") != 0 || raw == nullptr) {
            return std::nullopt;
        }
        std::string value(raw);
        std::free(raw);
        return value.empty() ? std::nullopt : std::optional<std::string>{std::move(value)};
#else
        const auto* raw = std::getenv("ENV");
        if (raw == nullptr || raw[0] == '\0') {
            return std::nullopt;
        }
        return std::string(raw);
#endif
    }();

    if (!env) {
        return false;
    }
    return toLowerCopy(*env) == "production";
}

bool isLocalCorsOrigin(const std::string& origin) {
    const auto normalized = toLowerCopy(origin);
    return normalized.find("localhost") != std::string::npos ||
           normalized.find("127.0.0.1") != std::string::npos ||
           normalized.find("[::1]") != std::string::npos ||
           normalized.find("0.0.0.0") != std::string::npos;
}

void validateProductionCors(const nlohmann::json& config) {
    if (!isProductionEnvironment() || !config.contains("cors") || !config.at("cors").is_object()) {
        return;
    }

    const auto& corsConfig = config.at("cors");
    if (!corsConfig.value("enabled", false)) {
        return;
    }

    const auto origins = corsConfig.value("allow_origins", std::vector<std::string>{});
    std::vector<std::string> unsafeOrigins;
    for (const auto& origin : origins) {
        if (origin == "*" || isLocalCorsOrigin(origin)) {
            unsafeOrigins.push_back(origin);
        }
    }

    if (!unsafeOrigins.empty()) {
        std::string message = "CORS allow_origins contains unsafe production origins:";
        for (const auto& origin : unsafeOrigins) {
            message += " " + origin;
        }
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        const auto config = backend::loadConfig();
        const auto& serverConfig = config.at("server");
        const auto& dbConfig = config.at("database");

        // --- JWT secret validation ---
        {
            const auto jwtSecret = config.at("jwt").value("secret", std::string());
            const std::vector<std::string> insecureSecrets = {
                "", "CHANGE_ME", "CHANGE_ME_JWT_SECRET", "change-me-via-JWT_SECRET",
                "development-secret-change-me"};
            for (const auto& insecure : insecureSecrets) {
                if (jwtSecret == insecure) {
                    spdlog::critical("JWT secret is not configured! "
                                     "Set JWT_SECRET env var or update jwt.secret in config.json");
                    return 1;
                }
            }
        }

        validateProductionCors(config);

        // --- Redis initialization ---
        try {
            if (config.contains("redis") && config.at("redis").is_object()) {
                auto redisConfig = redis::parseRedisConfig(config.at("redis"));
                if (redisConfig.enabled) {
                    redis::RedisClient::init(redisConfig);
                    if (redis::RedisClient::instance().ping())
                        spdlog::info("Redis ready: {}:{}", redisConfig.host, redisConfig.port);
                    else
                        spdlog::warn("Redis ping failed - falling back to DB polling");
                } else {
                    spdlog::info("Redis disabled by configuration");
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Redis init failed: {} - falling back to polling", e.what());
        }

        // --- Rate limiter initialization ---
        try {
            rate_limit::configureDefaultRateLimiter(config);
            if (rate_limit::defaultRateLimitConfig().enabled) {
                spdlog::info("Rate limiter enabled");
            } else {
                spdlog::info("Rate limiter disabled by configuration");
            }
        } catch (const std::exception& e) {
            spdlog::warn("Rate limiter init failed: {} - continuing fail-open", e.what());
        }

        // --- Cache initialization ---
        std::shared_ptr<cache::ICacheClient> cacheClient =
            std::make_shared<cache::NullCacheClient>();
        try {
            if (config.contains("cache") && config.at("cache").is_object()) {
                auto cacheConfig = parseCacheConfig(config.at("cache"));
                if (cacheConfig.enabled) {
                    cacheClient = std::make_shared<cache::RedisCacheClient>(cacheConfig);
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Cache init failed: {} - falling back to no cache", e.what());
            cacheClient = std::make_shared<cache::NullCacheClient>();
        }

        // Wrap with metrics decorator before injecting so all ImageService and TaskEngine
        // cache operations flow through MetricsCacheClient.
        auto cacheMetrics = std::make_shared<cache::CacheMetrics>();
        cacheClient = std::make_shared<cache::MetricsCacheClient>(cacheClient, cacheMetrics);
        MetricsController::setMetrics(cacheMetrics);
        spdlog::info("Cache metrics endpoint enabled at /api/metrics/cache");

        ImageService::setDefaultCache(cacheClient);

        // --- Database initialization ---

        const auto mysqlConfig = database::parseMysqlConfig(dbConfig);
        const auto configuredTaskWorkers =
            (std::max)(0, config.at("task_engine").value("workers", 2));
        const auto estimatedDbDemand = serverConfig.value("threads", 1) + configuredTaskWorkers + 1;
        metrics::MetricsRegistry::instance().setDbPoolCapacity(mysqlConfig.pool_size);
        metrics::MetricsRegistry::instance().setDbPoolStats(0, mysqlConfig.pool_size);
        if (mysqlConfig.pool_size < estimatedDbDemand) {
            spdlog::warn("database.pool_size={} is below estimated per-replica session demand "
                         "{} (server threads + task workers + main session). Increase "
                         "DB_POOL_SIZE or reduce BACKEND_THREADS/TASK_ENGINE_WORKERS before "
                         "raising replicas.",
                         mysqlConfig.pool_size, estimatedDbDemand);
        } else {
            spdlog::info("Database session budget: pool_size={}, estimated per-replica demand={}",
                         mysqlConfig.pool_size, estimatedDbDemand);
        }

        try {
            database::DBManager::init(mysqlConfig);
            ImageService::bootstrapWorkers(cacheClient);
            spdlog::info("Database initialized: {}:{}", mysqlConfig.host, mysqlConfig.port);
        } catch (const std::exception& e) {
            spdlog::warn("Database initialization failed: {}", e.what());
        }

        // --- MinIO initialization ---
        try {
            const auto& minioConfig = config.at("minio");
            const int presignExpiry = minioConfig.value("presign_expiry_seconds", 3600);
            ImageService::setPresignTtl(
                image_cache::derivePresignTtl(std::chrono::seconds(presignExpiry)));
            MinioClient::Config minioCfg;
            minioCfg.endpoint = minioConfig.value("endpoint", std::string("http://localhost:9000"));
            minioCfg.access_key = minioConfig.value("access_key", std::string());
            minioCfg.secret_key = minioConfig.value("secret_key", std::string());
            minioCfg.bucket = minioConfig.value("bucket", std::string("zimage"));
            minioCfg.region = minioConfig.value("region", std::string("us-east-1"));

            MinioClient minio(minioCfg);
            constexpr int kMinioMaxAttempts = 10;
            constexpr auto kMinioRetryDelay = std::chrono::seconds(2);
            bool minioReady = false;
            for (int attempt = 1; attempt <= kMinioMaxAttempts; ++attempt) {
                if (minio.ensureBucketExists()) {
                    minioReady = true;
                    break;
                }

                if (attempt < kMinioMaxAttempts) {
                    spdlog::warn("MinIO not ready yet ({}/{}), retrying in {}s", attempt,
                                 kMinioMaxAttempts, kMinioRetryDelay.count());
                    std::this_thread::sleep_for(kMinioRetryDelay);
                }
            }

            if (minioReady) {
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
                const bool dbHealthy = database::DBManager::isHealthy();
                if (!dbHealthy) {
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    resp->setBody(R"({"status":"unhealthy","database":"down"})");
                } else {
                    resp->setBody(R"({"status":"ok","database":"up"})");
                }
                callback(resp);
            };

        drogon::app().registerHandler("/health", healthHandler);
        drogon::app().registerHandler("/api/health", healthHandler);

        const auto host = serverConfig.value("host", std::string("0.0.0.0"));
        const auto port = serverConfig.value("port", 8082);
        const auto threads = serverConfig.value("threads", 1);

        // Limit request body to 1MB to prevent memory exhaustion from
        // oversized payloads (the create endpoint only needs prompt text).
        constexpr size_t kMaxBodySize = 1 * 1024 * 1024;

        // --- Request ID tracing (X-Request-Id) ---
        drogon::app().registerPreRoutingAdvice(
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&&,
               std::function<void()>&& accb) {
                auto requestId =
                    utils::sanitizeRequestId(req->getHeader(std::string(utils::kRequestIdHeader)));
                if (requestId.empty()) {
                    requestId = utils::generateRequestId();
                }
                req->attributes()->insert(std::string(utils::kRequestIdAttribute),
                                          std::move(requestId));
                accb();
            });

        drogon::app().registerPreSendingAdvice(
            [](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
                const auto attrs = req->attributes();
                const std::string attributeKey{utils::kRequestIdAttribute};
                if (attrs->find(attributeKey)) {
                    resp->addHeader(std::string(utils::kRequestIdHeader),
                                    attrs->get<std::string>(attributeKey));
                }
            });

        spdlog::info("Request ID tracing enabled (X-Request-Id)");

        // --- CORS setup ---
        if (config.contains("cors") && config.at("cors").value("enabled", false)) {
            const auto& corsConfig = config.at("cors");
            bool corsAllowAll = false;
            std::vector<std::string> corsOrigins;

            const auto joinHeaderList = [](const std::vector<std::string>& values) {
                std::string joined;
                for (size_t i = 0; i < values.size(); ++i) {
                    if (i > 0) {
                        joined += ", ";
                    }
                    joined += values[i];
                }
                return joined;
            };

            auto origins = corsConfig.value("allow_origins", std::vector<std::string>{});
            for (const auto& origin : origins) {
                if (origin == "*") {
                    corsAllowAll = true;
                }
                corsOrigins.push_back(origin);
            }

            const auto methods =
                corsConfig.value("allow_methods", std::vector<std::string>{"GET", "POST", "PUT",
                                                                           "DELETE", "OPTIONS"});

            const auto headers = corsConfig.value(
                "allow_headers",
                std::vector<std::string>{"Content-Type", "Authorization", "X-Request-Id"});
            const auto corsAllowMethods = joinHeaderList(methods);
            const auto corsAllowHeaders = joinHeaderList(headers);

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
