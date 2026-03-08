#include "ImageRepo.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
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
    "height, width, seed, status, image_url, image_base64, error_message, "
    "generation_time, created_at, completed_at";

std::string imageTableName()
{
    static const std::string tableName = [] {
        const auto config = backend::loadConfig();
        const auto dbName = config.at("database").at("database").get<std::string>();
        if (dbName.empty()) {
            throw std::runtime_error("database.database is empty in config.json");
        }
        return "`" + dbName + "`.`image_generations`";
    }();

    return tableName;
}

std::string timeToDbString(const std::chrono::system_clock::time_point& tp)
{
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_s(&tm, &time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
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
    std::istringstream iss(value);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) {
        return std::nullopt;
    }

    const auto asTimeT = std::mktime(&tm);
    if (asTimeT == -1) {
        return std::nullopt;
    }

    return std::chrono::system_clock::from_time_t(asTimeT);
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
    image.image_url = getStringOrEmpty(row, 10);
    image.image_base64 = getStringOrEmpty(row, 11);
    image.error_message = getStringOrEmpty(row, 12);
    image.generation_time = getDoubleOrDefault(row, 13, 0.0);

    if (const auto createdAt = parseDbTime(getStringOrEmpty(row, 14))) {
        image.created_at = *createdAt;
    } else {
        image.created_at = std::chrono::system_clock::now();
    }

    if (const auto completedAt = parseDbTime(getStringOrEmpty(row, 15))) {
        image.completed_at = completedAt;
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

} // namespace

void ImageRepo::ensureTable()
{
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            database::DBManager::session().sql(
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
                    image_url VARCHAR(500) DEFAULT NULL,
                    image_base64 LONGTEXT DEFAULT NULL,
                    error_message TEXT DEFAULT NULL,
                    generation_time DOUBLE DEFAULT NULL,
                    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                    completed_at DATETIME DEFAULT NULL,
                    INDEX idx_user_id (user_id),
                    INDEX idx_request_id (request_id),
                    INDEX idx_created_at (created_at)
                )
            )")
                .execute();
        } catch (const mysqlx::Error& ex) {
            spdlog::error("ImageRepo::ensureTable mysqlx error: {}", ex.what());
            throw std::runtime_error(std::string("failed to create image_generations table: ") + ex.what());
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

    mysqlx::Value seedValue = generation.seed.has_value()
        ? mysqlx::Value(generation.seed.value())
        : mysqlx::Value();
    mysqlx::Value completedAtValue = generation.completed_at.has_value()
        ? mysqlx::Value(timeToDbString(generation.completed_at.value()))
        : mysqlx::Value();

    database::DBManager::session().sql(
        "INSERT INTO " + imageTableName() + R"(
            (user_id, request_id, prompt, negative_prompt, num_steps,
             height, width, seed, status, image_url, image_base64,
             error_message, generation_time, created_at, completed_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
              generation.image_url,
              generation.image_base64,
              generation.error_message,
              generation.generation_time,
              timeToDbString(createdAt),
              completedAtValue)
        .execute();

    auto idResult = database::DBManager::session().sql("SELECT LAST_INSERT_ID()").execute();
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

    auto result = database::DBManager::session().sql(
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

    auto result = database::DBManager::session().sql(
        std::string("SELECT ") + kColumns +
        " FROM " + imageTableName() + " WHERE user_id = ? AND status = ? ORDER BY id DESC LIMIT ? OFFSET ?")
        .bind(userId, status, safeSize, offset)
        .execute();

    return collectResultRows(result);
}

int64_t ImageRepo::countByUserId(int64_t userId)
{
    ensureTable();

    auto result = database::DBManager::session()
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

    auto result = database::DBManager::session()
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

    auto result = database::DBManager::session().sql(
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

    auto result = database::DBManager::session()
        .sql("DELETE FROM " + imageTableName() + " WHERE id = ? AND user_id = ?")
        .bind(id, userId)
        .execute();

    return result.getAffectedItemsCount() > 0;
}
