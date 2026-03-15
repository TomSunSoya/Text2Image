#include "ImageRepo.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

#include <mysqlx/xdevapi.h>
#include <spdlog/spdlog.h>

#include "Backend.h"
#include "db_manager.h"

namespace {

constexpr const char* kColumns =
    "id, user_id, request_id, prompt, negative_prompt, num_steps, "
    "height, width, seed, status, retry_count, max_retries, failure_code, "
    "worker_id, image_url, thumbnail_url, storage_key, image_base64, "
    "error_message, generation_time, created_at, started_at, completed_at, "
    "cancelled_at, lease_expires_at";

constexpr const char* kImageTable = "image_generations";

std::string imageSchemaName()
{
    static const std::string dbName = [] {
        const auto config = backend::loadConfig();
        const auto value = config.at("database").at("database").get<std::string>();
        if (value.empty()) {
            throw std::runtime_error("database.database is empty in config.json");
        }
        return value;
    }();

    return dbName;
}

std::string imageTableName()
{
    static const std::string tableName = "`" + imageSchemaName() + "`.`" + std::string(kImageTable) + "`";
    return tableName;
}

std::string timeToDbString(const std::chrono::system_clock::time_point& tp)
{
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
}

std::optional<std::chrono::system_clock::time_point> parseDbTime(std::string value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    const auto dotPos = value.find('.');
    if (dotPos != std::string::npos) {
        value = value.substr(0, dotPos);
    }

    for (auto& ch : value) {
        if (ch == 'T') {
            ch = ' ';
        }
    }

    std::tm tm{};
    if (std::sscanf(value.c_str(), "%d-%d-%d %d:%d:%d",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
        return std::nullopt;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;

    const auto asTimeT = std::mktime(&tm);
    if (asTimeT == -1) {
        return std::nullopt;
    }

    return std::chrono::system_clock::from_time_t(asTimeT);
}

mysqlx::Value optionalTimeToValue(const std::optional<std::chrono::system_clock::time_point>& value)
{
    return value.has_value() ? mysqlx::Value(timeToDbString(*value)) : mysqlx::Value();
}

bool columnExists(const std::string& schemaName, const std::string& tableName, const std::string& columnName)
{
    auto result = database::DBManager::threadSession().sql(
        "SELECT COUNT(*) FROM information_schema.columns "
        "WHERE table_schema = ? AND table_name = ? AND column_name = ?")
        .bind(schemaName, tableName, columnName)
        .execute();

    auto row = result.fetchOne();
    return row && !row[0].isNull() && row[0].get<uint64_t>() > 0;
}

bool indexExists(const std::string& schemaName, const std::string& tableName, const std::string& indexName)
{
    auto result = database::DBManager::threadSession().sql(
        "SELECT COUNT(*) FROM information_schema.statistics "
        "WHERE table_schema = ? AND table_name = ? AND index_name = ?")
        .bind(schemaName, tableName, indexName)
        .execute();

    auto row = result.fetchOne();
    return row && !row[0].isNull() && row[0].get<uint64_t>() > 0;
}

std::string getStringOrEmpty(const mysqlx::Row& row, int index)
{
    return row[index].isNull() ? std::string{} : row[index].get<std::string>();
}

int getIntOrDefault(const mysqlx::Row& row, int index, int fallback)
{
    if (row[index].isNull()) {
        return fallback;
    }
    return static_cast<int>(row[index].get<int64_t>());
}

double getDoubleOrDefault(const mysqlx::Row& row, int index, double fallback)
{
    if (row[index].isNull()) {
        return fallback;
    }
    return row[index].get<double>();
}

models::ImageGeneration rowToImageGeneration(const mysqlx::Row& row)
{
    models::ImageGeneration image;
    image.id = static_cast<int64_t>(row[0].get<uint64_t>());
    image.user_id = static_cast<int64_t>(row[1].get<uint64_t>());
    image.request_id = getStringOrEmpty(row, 2);
    image.prompt = getStringOrEmpty(row, 3);
    image.negative_prompt = getStringOrEmpty(row, 4);
    image.num_steps = getIntOrDefault(row, 5, 8);
    image.height = getIntOrDefault(row, 6, 768);
    image.width = getIntOrDefault(row, 7, 768);
    if (!row[8].isNull()) {
        image.seed = static_cast<int>(row[8].get<int64_t>());
    }
    image.status = getStringOrEmpty(row, 9);
    image.retry_count = getIntOrDefault(row, 10, 0);
    image.max_retries = getIntOrDefault(row, 11, 3);
    image.failure_code = getStringOrEmpty(row, 12);
    image.worker_id = getStringOrEmpty(row, 13);
    image.image_url = getStringOrEmpty(row, 14);
    image.thumbnail_url = getStringOrEmpty(row, 15);
    image.storage_key = getStringOrEmpty(row, 16);
    image.image_base64 = getStringOrEmpty(row, 17);
    image.error_message = getStringOrEmpty(row, 18);
    image.generation_time = getDoubleOrDefault(row, 19, 0.0);

    if (const auto createdAt = parseDbTime(getStringOrEmpty(row, 20))) {
        image.created_at = *createdAt;
    } else {
        image.created_at = std::chrono::system_clock::now();
    }

    if (const auto startedAt = parseDbTime(getStringOrEmpty(row, 21))) {
        image.started_at = startedAt;
    }

    if (const auto completedAt = parseDbTime(getStringOrEmpty(row, 22))) {
        image.completed_at = completedAt;
    }

    if (const auto cancelledAt = parseDbTime(getStringOrEmpty(row, 23))) {
        image.cancelled_at = cancelledAt;
    }

    if (const auto leaseExpiresAt = parseDbTime(getStringOrEmpty(row, 24))) {
        image.lease_expires_at = leaseExpiresAt;
    }

    return image;
}

int normalizePage(int page)
{
    return page < 0 ? 0 : page;
}

int normalizeSize(int size)
{
    return size <= 0 ? 10 : size;
}

std::vector<models::ImageGeneration> collectResultRows(mysqlx::SqlResult& result)
{
    std::vector<models::ImageGeneration> images;
    while (true) {
        auto row = result.fetchOne();
        if (!row) {
            break;
        }
        images.push_back(rowToImageGeneration(row));
    }
    return images;
}

bool isTerminalStatus(const std::string& status)
{
    return status == "success" || status == "failed" || status == "cancelled" || status == "timeout";
}

} // namespace

void ImageRepo::ensureTable()
{
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            const auto schemaName = imageSchemaName();
        
            database::DBManager::threadSession().sql(
                "CREATE TABLE IF NOT EXISTS " + imageTableName() + R"(
                (
                    id BIGINT AUTO_INCREMENT PRIMARY KEY,
                    user_id BIGINT NOT NULL,
                    request_id VARCHAR(64) DEFAULT NULL,
                    prompt TEXT,
                    negative_prompt TEXT,
                    num_steps INT DEFAULT NULL,
                    height INT DEFAULT NULL,
                    width INT DEFAULT NULL,
                    seed INT DEFAULT NULL,
                    status VARCHAR(20) DEFAULT NULL,
                    retry_count INT NOT NULL DEFAULT 0,
                    max_retries INT NOT NULL DEFAULT 3,
                    failure_code VARCHAR(64) DEFAULT NULL,
                    worker_id VARCHAR(128) DEFAULT NULL,
                    image_url VARCHAR(500) DEFAULT NULL,
                    thumbnail_url VARCHAR(500) DEFAULT NULL,
                    storage_key VARCHAR(255) DEFAULT NULL,
                    image_base64 LONGTEXT DEFAULT NULL,
                    error_message TEXT DEFAULT NULL,
                    generation_time DOUBLE DEFAULT NULL,
                    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                    started_at DATETIME DEFAULT NULL,
                    completed_at DATETIME DEFAULT NULL,
                    cancelled_at DATETIME DEFAULT NULL,
                    lease_expires_at DATETIME DEFAULT NULL,
                    INDEX idx_user_id (user_id),
                    INDEX idx_request_id (request_id),
                    INDEX idx_created_at (created_at),
                    INDEX idx_status_created_at (status, created_at),
                    INDEX idx_status_lease (status, lease_expires_at)
                )
            )")
                .execute();

            const std::vector<std::pair<std::string, std::string>> columnMigrations = {
                {"retry_count", "retry_count INT NOT NULL DEFAULT 0"},
                {"max_retries", "max_retries INT NOT NULL DEFAULT 3"},
                {"failure_code", "failure_code VARCHAR(64) DEFAULT NULL"},
                {"worker_id", "worker_id VARCHAR(128) DEFAULT NULL"},
                {"thumbnail_url", "thumbnail_url VARCHAR(500) DEFAULT NULL"},
                {"storage_key", "storage_key VARCHAR(255) DEFAULT NULL"},
                {"started_at", "started_at DATETIME DEFAULT NULL"},
                {"cancelled_at", "cancelled_at DATETIME DEFAULT NULL"},
                {"lease_expires_at", "lease_expires_at DATETIME DEFAULT NULL"}
            };

            for (const auto& [columnName, definition] : columnMigrations) {
                if (columnExists(schemaName, kImageTable, columnName)) {
                    continue;
                }

                database::DBManager::threadSession().sql(
                    "ALTER TABLE " + imageTableName() + " ADD COLUMN " + definition)
                    .execute();
            }

            const std::vector<std::pair<std::string, std::string>> indexMigrations = {
                {"idx_status_created_at", "ADD INDEX idx_status_created_at (status, created_at)"},
                {"idx_status_lease", "ADD INDEX idx_status_lease (status, lease_expires_at)"}
            };

            for (const auto& [indexName, definition] : indexMigrations) {
                if (indexExists(schemaName, kImageTable, indexName)) {
                    continue;
                }

                database::DBManager::threadSession().sql(
                    "ALTER TABLE " + imageTableName() + " " + definition)
                    .execute();
            }
        } catch (const mysqlx::Error& ex) {
            spdlog::error("ImageRepo::ensureTable mysqlx error: {}", ex.what());
            throw std::runtime_error(std::string("failed to initialize image_generations table: ") + ex.what());
        }
    });
}

int64_t ImageRepo::insert(const models::ImageGeneration& generation)
{
    ensureTable();

    auto createdAt = generation.created_at;
    if (createdAt.time_since_epoch().count() == 0) {
        createdAt = std::chrono::system_clock::now();
    }

    mysqlx::Value seedValue = generation.seed.has_value() ? mysqlx::Value(generation.seed.value()) : mysqlx::Value();



    database::DBManager::threadSession().sql(
        "INSERT INTO " + imageTableName() + R"(
            (user_id, request_id, prompt, negative_prompt, num_steps, height, width,
             seed, status, retry_count, max_retries, failure_code, worker_id, image_url,
             thumbnail_url, storage_key, image_base64, error_message, generation_time,
             created_at, started_at, completed_at, cancelled_at, lease_expires_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )")
        .bind(generation.user_id,
              generation.request_id,
              generation.prompt,
              generation.negative_prompt,
              generation.num_steps,
              generation.height,
              generation.width,
              seedValue,
              generation.status,
              generation.retry_count,
              generation.max_retries,
              generation.failure_code,
              generation.worker_id,
              generation.image_url,
              generation.thumbnail_url,
              generation.storage_key,
              generation.image_base64,
              generation.error_message,
              generation.generation_time,
              timeToDbString(createdAt),
              optionalTimeToValue(generation.started_at),
              optionalTimeToValue(generation.completed_at),
              optionalTimeToValue(generation.cancelled_at),
              optionalTimeToValue(generation.lease_expires_at))
        .execute();

    auto idResult = database::DBManager::threadSession().sql("SELECT LAST_INSERT_ID()").execute();
    auto idRow = idResult.fetchOne();
    if (!idRow || idRow[0].isNull()) {
        throw std::runtime_error("failed to fetch inserted image id");
    }

    return static_cast<int64_t>(idRow[0].get<uint64_t>());
}

std::vector<models::ImageGeneration> ImageRepo::findByUserId(int64_t userId, int page, int size)
{
    ensureTable();

    const int safePage = normalizePage(page);
    const int safeSize = normalizeSize(size);
    const int64_t offset = static_cast<int64_t>(safePage) * static_cast<int64_t>(safeSize);


    auto result = database::DBManager::threadSession().sql(
        std::string("SELECT ") + kColumns +
        " FROM " + imageTableName() + " WHERE user_id = ? ORDER BY id DESC LIMIT ? OFFSET ?")
        .bind(userId, safeSize, offset)
        .execute();

    return collectResultRows(result);
}

std::vector<models::ImageGeneration> ImageRepo::findByUserIdAndStatus(int64_t userId,
                                                                       const std::string& status,
                                                                       int page,
                                                                       int size)
{
    ensureTable();

    const int safePage = normalizePage(page);
    const int safeSize = normalizeSize(size);
    const int64_t offset = static_cast<int64_t>(safePage) * static_cast<int64_t>(safeSize);


    auto result = database::DBManager::threadSession().sql(
        std::string("SELECT ") + kColumns +
        " FROM " + imageTableName() + " WHERE user_id = ? AND status = ? ORDER BY id DESC LIMIT ? OFFSET ?")
        .bind(userId, status, safeSize, offset)
        .execute();

    return collectResultRows(result);
}

int64_t ImageRepo::countByUserId(int64_t userId)
{
    ensureTable();


    auto result = database::DBManager::threadSession()
        .sql("SELECT COUNT(*) FROM " + imageTableName() + " WHERE user_id = ?")
        .bind(userId)
        .execute();

    auto row = result.fetchOne();
    if (!row || row[0].isNull()) {
        return 0;
    }

    return static_cast<int64_t>(row[0].get<uint64_t>());
}

int64_t ImageRepo::countByUserIdAndStatus(int64_t userId, const std::string& status)
{
    ensureTable();


    auto result = database::DBManager::threadSession()
        .sql("SELECT COUNT(*) FROM " + imageTableName() + " WHERE user_id = ? AND status = ?")
        .bind(userId, status)
        .execute();

    auto row = result.fetchOne();
    if (!row || row[0].isNull()) {
        return 0;
    }

    return static_cast<int64_t>(row[0].get<uint64_t>());
}

std::optional<models::ImageGeneration> ImageRepo::findByIdAndUserId(int64_t id, int64_t userId)
{
    ensureTable();


    auto result = database::DBManager::threadSession().sql(
        std::string("SELECT ") + kColumns +
        " FROM " + imageTableName() + " WHERE id = ? AND user_id = ?")
        .bind(id, userId)
        .execute();

    auto row = result.fetchOne();
    if (!row) {
        return std::nullopt;
    }

    return rowToImageGeneration(row);
}

bool ImageRepo::deleteByIdAndUserId(int64_t id, int64_t userId)
{
    ensureTable();


    auto result = database::DBManager::threadSession()
        .sql("DELETE FROM " + imageTableName() + " WHERE id = ? AND user_id = ?")
        .bind(id, userId)
        .execute();

    return result.getAffectedItemsCount() > 0;
}

std::optional<models::ImageGeneration> ImageRepo::findByRequestIdAndUserId(const std::string& requestId, int64_t userId)
{
	ensureTable();


    auto result = database::DBManager::threadSession().sql(
        std::string("SELECT ") + kColumns +
        " FROM " + imageTableName() + " WHERE request_id = ? AND user_id = ?")
        .bind(requestId, userId)
        .execute();

    auto row = result.fetchOne();
    if (!row) {
        return std::nullopt;
    }

    return rowToImageGeneration(row);
}

std::optional<models::ImageGeneration> ImageRepo::claimNextTask(const std::string& workerId, long leaseSeconds)
{
    ensureTable();

    const auto now = std::chrono::system_clock::now();
    const auto expiresAt = now + std::chrono::seconds(leaseSeconds <= 0 ? 300 : leaseSeconds);
    const auto nowText = timeToDbString(now);
    const auto expiresAtText = timeToDbString(expiresAt);



    auto select = database::DBManager::threadSession().sql(
        std::string("SELECT ") + kColumns +
        " FROM " + imageTableName() +
        " WHERE status IN ('queued', 'pending') "
        "    OR (status = 'generating' AND (lease_expires_at IS NULL OR lease_expires_at < ?)) "
        " ORDER BY created_at ASC, id ASC LIMIT 1")
        .bind(nowText)
        .execute();

    auto row = select.fetchOne();
    if (!row) {
        return std::nullopt;
    }

    auto task = rowToImageGeneration(row);
    auto update = database::DBManager::threadSession().sql(
        "UPDATE " + imageTableName() +
        " SET status = 'generating', worker_id = ?, started_at = IFNULL(started_at, ?), lease_expires_at = ?, failure_code = '', error_message = ''"
        " WHERE id = ? AND (status IN ('queued', 'pending') OR (status = 'generating' AND (lease_expires_at IS NULL OR lease_expires_at < ?)))")
        .bind(workerId, nowText, expiresAtText, task.id, nowText)
        .execute();

    if (update.getAffectedItemsCount() == 0) {
        return std::nullopt;
    }

    task.status = "generating";
    task.worker_id = workerId;
    if (!task.started_at.has_value()) {
        task.started_at = now;
    }

    task.lease_expires_at = expiresAt;
    task.failure_code.clear();
    task.error_message.clear();
    return task;
}

bool ImageRepo::finishClaimedTask(const models::ImageGeneration& generation)
{
    ensureTable();


    auto result = database::DBManager::threadSession().sql(
        "UPDATE " + imageTableName() +
        " SET status = ?, image_url = ?, image_base64 = ?, error_message = ?, generation_time = ?, completed_at = ?, cancelled_at = ?,"
        " failure_code = ?, thumbnail_url = ?, storage_key = ?, lease_expires_at = NULL, worker_id = NULL "
        " WHERE id = ? AND user_id = ? AND status = 'generating' AND worker_id = ?")
        .bind(generation.status,
              generation.image_url,
              generation.image_base64,
              generation.error_message,
              generation.generation_time,
              optionalTimeToValue(generation.completed_at),
              optionalTimeToValue(generation.cancelled_at),
              generation.failure_code,
              generation.thumbnail_url,
              generation.storage_key,
              generation.id,
              generation.user_id)
        .bind(generation.worker_id)
        .execute();

    return result.getAffectedItemsCount() > 0;
}

bool ImageRepo::cancelByIdAndUserId(int64_t id, int64_t userId, models::ImageGeneration* updated)
{
    ensureTable();

    const auto nowText = timeToDbString(std::chrono::system_clock::now());


    auto result = database::DBManager::threadSession().sql(
        "UPDATE " + imageTableName() +
        " SET status = 'cancelled', cancelled_at = ?, lease_expires_at = NULL, worker_id = NULL "
        "   , error_message = '', failure_code = '', completed_at = ?"
        " WHERE id = ? AND user_id = ? AND status IN ('queued', 'pending', 'generating')")
        .bind(nowText, nowText, id, userId)
        .execute();

    if (result.getAffectedItemsCount() == 0) {
        return false;
    }

    if (updated) {
        auto select = database::DBManager::threadSession().sql(
            std::string("SELECT ") + kColumns +
            " FROM " + imageTableName() + " WHERE id = ? AND user_id = ?")
            .bind(id, userId)
            .execute();

        auto row = select.fetchOne();
        if (row)
            *updated = rowToImageGeneration(row);
    }
    return true;
}

bool ImageRepo::retryByIdAndUserId(int64_t id, int64_t userId, models::ImageGeneration* updated)
{
    ensureTable();


    auto result = database::DBManager::threadSession().sql(
        "UPDATE " + imageTableName() +
        " SET status = 'queued', retry_count = retry_count + 1, failure_code = '', error_message = '', "
        " image_url = '', thumbnail_url = '', storage_key = '', image_base64 = '', generation_time = 0, "
        " worker_id = NULL, lease_expires_at = NULL, started_at = NULL, completed_at = NULL, cancelled_at = NULL "
        " WHERE id = ? AND user_id = ? AND status IN ('failed', 'timeout', 'cancelled') AND retry_count < max_retries")
        .bind(id, userId)
        .execute();

    if (result.getAffectedItemsCount() == 0) {
        return false;
    }

    if (updated) {
        auto select = database::DBManager::threadSession().sql(
            std::string("SELECT ") + kColumns +
            " FROM " + imageTableName() + " WHERE id = ? AND user_id = ?")
            .bind(id, userId)
            .execute();

        auto row = select.fetchOne();
        if (row) {
            *updated = rowToImageGeneration(row);
        }
    }

    return true;
}

bool ImageRepo::updateStatusAndError(int64_t id,
                                     int64_t userId,
                                     const std::string& status,
                                     const std::string& errorMessage)
{
    ensureTable();

    mysqlx::Value completedAt = isTerminalStatus(status)
        ? mysqlx::Value(timeToDbString(std::chrono::system_clock::now()))
        : mysqlx::Value();


    auto result = database::DBManager::threadSession().sql(
        "UPDATE " + imageTableName() +
        " SET status = ?, error_message = ?, completed_at = ? WHERE id = ? AND user_id = ?")
        .bind(status, errorMessage, completedAt, id, userId)
        .execute();

    return result.getAffectedItemsCount() > 0;
}

bool ImageRepo::updateGenerationResult(
    int64_t id,
    int64_t userId,
    const std::string& status,
    const std::string& imageUrl,
    const std::string& imageBase64,
    const std::string& errorMessage,
    double generationTime,
    const std::string& failureCode,
    const std::string& thumbnailUrl,
    const std::string& storageKey,
    const std::optional<std::chrono::system_clock::time_point>& completedAt)
{
    ensureTable();

    mysqlx::Value completedAtValue = completedAt.has_value()
        ? mysqlx::Value(timeToDbString(completedAt.value()))
        : mysqlx::Value();


    auto result = database::DBManager::threadSession().sql(
        "UPDATE " + imageTableName() +
        " SET status = ?, image_url = ?, image_base64 = ?, error_message = ?, "
        " generation_time = ?, failure_code = ?, thumbnail_url = ?, storage_key = ?, completed_at = ?"
        " WHERE id = ? AND user_id = ?")
        .bind(status,
              imageUrl,
              imageBase64,
              errorMessage,
              generationTime,
              failureCode,
              thumbnailUrl,
              storageKey,
              completedAtValue,
              id,
              userId)
        .execute();

    return result.getAffectedItemsCount() > 0;
}
