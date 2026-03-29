#include "Backend.h"

#include "utils/string_utils.h"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace backend {

namespace {

std::filesystem::path executableDir() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const auto len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) {
        return {};
    }

    std::vector<char> buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }

    std::error_code ec;
    auto resolved = std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()), ec);
    if (ec) {
        resolved = std::filesystem::path(buffer.data());
    }
    return resolved.parent_path();
#else
    std::vector<char> buffer(4096, '\0');
    const auto len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (len <= 0) {
        return {};
    }

    buffer[static_cast<size_t>(len)] = '\0';
    return std::filesystem::path(buffer.data()).parent_path();
#endif
}

void appendCandidatePath(std::vector<std::filesystem::path>& candidates,
                         const std::filesystem::path& baseDir, const std::filesystem::path& input) {
    candidates.push_back(baseDir / input);
    candidates.push_back(baseDir / "etc" / input);
}

std::vector<std::filesystem::path> buildCandidatePaths(const std::string& path) {
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path input(path);

    candidates.push_back(input);

    if (input.is_relative()) {
        appendCandidatePath(candidates, std::filesystem::current_path(), input);

        const auto exeDir = executableDir();
        if (!exeDir.empty()) {
            appendCandidatePath(candidates, exeDir, input);

            auto walk = exeDir;
            for (int i = 0; i < 8 && walk.has_parent_path(); ++i) {
                walk = walk.parent_path();
                appendCandidatePath(candidates, walk, input);
            }
        }
    }

    return candidates;
}

std::optional<std::string> readEnv(const char* name) {
#ifdef _WIN32
    char* raw = nullptr;
    size_t size = 0;
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
        return std::nullopt;
    }

    std::string value(raw);
    free(raw);
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) {
        return std::nullopt;
    }

    std::string value(raw);
#endif
    if (value.empty()) {
        return std::nullopt;
    }

    return value;
}

void overrideString(nlohmann::json& object, const char* key, const char* envName) {
    if (auto value = readEnv(envName)) {
        object[key] = *value;
    }
}

void overrideInt(nlohmann::json& object, const char* key, const char* envName) {
    if (auto value = readEnv(envName)) {
        try {
            object[key] = std::stoi(*value);
        } catch (...) {
        }
    }
}

void overrideBool(nlohmann::json& object, const char* key, const char* envName) {
    if (auto value = readEnv(envName)) {
        if (auto parsed = utils::parseBool(*value)) {
            object[key] = *parsed;
        }
    }
}

void applyEnvOverrides(nlohmann::json& config) {
    auto& server = config["server"];
    if (!server.is_object()) {
        server = nlohmann::json::object();
    }

    auto& database = config["database"];
    if (!database.is_object()) {
        database = nlohmann::json::object();
    }

    auto& jwt = config["jwt"];
    if (!jwt.is_object()) {
        jwt = nlohmann::json::object();
    }

    auto& pythonService = config["python_service"];
    if (!pythonService.is_object()) {
        pythonService = nlohmann::json::object();
    }

    auto& taskEngine = config["task_engine"];
    if (!taskEngine.is_object()) {
        taskEngine = nlohmann::json::object();
    }

    auto& storage = config["storage"];
    if (!storage.is_object()) {
        storage = nlohmann::json::object();
    }

    auto& minio = config["minio"];
    if (!minio.is_object()) {
        minio = nlohmann::json::object();
    }

    auto& redis = config["redis"];
    if (!redis.is_object()) {
        redis = nlohmann::json::object();
    }

    overrideString(redis, "host", "REDIS_HOST");
    overrideInt(redis, "port", "REDIS_PORT");
    overrideString(redis, "password", "REDIS_PASSWORD");
    overrideInt(redis, "db", "REDIS_DB");
    overrideInt(redis, "pool_size", "REDIS_POOL_SIZE");
    overrideInt(redis, "connect_timeout_ms", "REDIS_CONNECT_TIMEOUT_MS");
    overrideInt(redis, "socket_timeout_ms", "REDIS_SOCKET_TIMEOUT_MS");
    overrideString(redis, "task_queue_key", "REDIS_TASK_QUEUE_KEY");
    overrideString(redis, "lease_key_prefix", "REDIS_LEASE_KEY_PREFIX");
    overrideBool(redis, "enabled", "REDIS_ENABLED");

    overrideString(server, "host", "BACKEND_HOST");
    overrideInt(server, "port", "BACKEND_PORT");
    overrideInt(server, "threads", "BACKEND_THREADS");

    overrideString(database, "host", "DB_HOST");
    overrideInt(database, "port", "DB_PORT");
    overrideString(database, "username", "DB_USERNAME");
    overrideString(database, "password", "DB_PASSWORD");
    overrideString(database, "database", "DB_NAME");
    overrideBool(database, "ssl", "DB_SSL");

    overrideString(jwt, "secret", "JWT_SECRET");
    overrideInt(jwt, "expiration_hours", "JWT_EXPIRATION_HOURS");

    overrideString(pythonService, "url", "PYTHON_SERVICE_URL");
    overrideInt(pythonService, "timeout_seconds", "PYTHON_SERVICE_TIMEOUT_SECONDS");

    overrideInt(taskEngine, "workers", "TASK_ENGINE_WORKERS");
    overrideInt(taskEngine, "poll_interval_ms", "TASK_ENGINE_POLL_INTERVAL_MS");
    overrideInt(taskEngine, "lease_seconds", "TASK_ENGINE_LEASE_SECONDS");
    overrideInt(taskEngine, "max_retries", "TASK_ENGINE_MAX_RETRIES");
    overrideString(taskEngine, "worker_prefix", "TASK_ENGINE_WORKER_PREFIX");

    overrideString(storage, "root_dir", "STORAGE_ROOT_DIR");
    overrideString(storage, "public_url_prefix", "STORAGE_PUBLIC_URL_PREFIX");
    overrideString(storage, "extension", "STORAGE_EXTENSION");

    overrideString(minio, "endpoint", "MINIO_ENDPOINT");
    overrideString(minio, "access_key", "MINIO_ACCESS_KEY");
    overrideString(minio, "secret_key", "MINIO_SECRET_KEY");
    overrideString(minio, "bucket", "MINIO_BUCKET");
    overrideString(minio, "region", "MINIO_REGION");
    overrideInt(minio, "presign_expiry_seconds", "MINIO_PRESIGN_EXPIRY_SECONDS");
}

} // namespace

nlohmann::json loadConfig(const std::string& path) {
    const auto effectivePath = readEnv("BACKEND_CONFIG_PATH").value_or(path);
    auto candidates = buildCandidatePaths(effectivePath);
    const std::filesystem::path inputPath(effectivePath);
    if (inputPath.filename() == "config.json") {
        auto fallback = inputPath;
        fallback += ".example";
        const auto fallbackCandidates = buildCandidatePaths(fallback.string());
        candidates.insert(candidates.end(), fallbackCandidates.begin(), fallbackCandidates.end());
    }

    std::vector<std::filesystem::path> tried;
    for (const auto& candidate : candidates) {
        tried.push_back(candidate);

        std::ifstream configFile(candidate);
        if (!configFile.is_open()) {
            continue;
        }

        nlohmann::json config;
        configFile >> config;
        applyEnvOverrides(config);
        return config;
    }

    std::string message = std::format("failed to open config file: {} (tried:", effectivePath);
    for (const auto& p : tried) {
        message += std::format(" {}", p.string());
    }
    message += ")";
    throw std::runtime_error(message);
}

const nlohmann::json& cachedConfig() {
    static const nlohmann::json config = loadConfig();
    return config;
}

} // namespace backend
