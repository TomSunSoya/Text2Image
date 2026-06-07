#include "Backend.h"

#include "utils/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
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

std::string readSecretFile(const std::string& path, const char* envName) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            std::format("{} points to unreadable secret file: {}", envName, path));
    }

    std::string value((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    if (value.empty()) {
        throw std::runtime_error(std::format("{} points to empty secret file: {}", envName, path));
    }

    return value;
}

std::optional<std::string> readEnvOrSecretFile(const char* envName, const char* fileEnvName) {
    if (auto path = readEnv(fileEnvName)) {
        return readSecretFile(*path, fileEnvName);
    }
    return readEnv(envName);
}

void overrideSecretString(nlohmann::json& object, const char* key, const char* envName,
                          const char* fileEnvName) {
    if (auto value = readEnvOrSecretFile(envName, fileEnvName)) {
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

std::string trimCopy(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::vector<std::string> parseCsvList(const std::string& value) {
    std::vector<std::string> items;
    size_t start = 0;

    while (start <= value.size()) {
        const auto end = value.find(',', start);
        auto item = trimCopy(value.substr(start, end == std::string::npos ? end : end - start));
        if (!item.empty()) {
            items.push_back(std::move(item));
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return items;
}

void overrideStringList(nlohmann::json& object, const char* key, const char* envName) {
    if (auto value = readEnv(envName)) {
        object[key] = parseCsvList(*value);
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

    auto& cache = config["cache"];
    if (!cache.is_object()) {
        cache = nlohmann::json::object();
    }

    auto& rateLimit = config["rate_limit"];
    if (!rateLimit.is_object()) {
        rateLimit = nlohmann::json::object();
    }

    auto& cors = config["cors"];
    if (!cors.is_object()) {
        cors = nlohmann::json::object();
    }

    overrideString(redis, "host", "REDIS_HOST");
    overrideInt(redis, "port", "REDIS_PORT");
    overrideSecretString(redis, "password", "REDIS_PASSWORD", "REDIS_PASSWORD_FILE");
    overrideInt(redis, "db", "REDIS_DB");
    overrideInt(redis, "pool_size", "REDIS_POOL_SIZE");
    overrideInt(redis, "connect_timeout_ms", "REDIS_CONNECT_TIMEOUT_MS");
    overrideInt(redis, "socket_timeout_ms", "REDIS_SOCKET_TIMEOUT_MS");
    overrideString(redis, "task_queue_key", "REDIS_TASK_QUEUE_KEY");
    overrideString(redis, "lease_key_prefix", "REDIS_LEASE_KEY_PREFIX");
    overrideBool(redis, "enabled", "REDIS_ENABLED");

    overrideString(cache, "host", "CACHE_HOST");
    overrideInt(cache, "port", "CACHE_PORT");
    overrideSecretString(cache, "password", "CACHE_PASSWORD", "CACHE_PASSWORD_FILE");
    overrideInt(cache, "db", "CACHE_DB");
    overrideInt(cache, "pool_size", "CACHE_POOL_SIZE");
    overrideInt(cache, "connect_timeout_ms", "CACHE_CONNECT_TIMEOUT_MS");
    overrideInt(cache, "socket_timeout_ms", "CACHE_SOCKET_TIMEOUT_MS");
    overrideString(cache, "key_prefix", "CACHE_KEY_PREFIX");
    overrideInt(cache, "version_key_ttl_seconds", "CACHE_VERSION_KEY_TTL_SECONDS");
    overrideBool(cache, "enabled", "CACHE_ENABLED");

    overrideBool(rateLimit, "enabled", "RATE_LIMIT_ENABLED");
    overrideBool(rateLimit, "fail_open", "RATE_LIMIT_FAIL_OPEN");
    overrideBool(rateLimit, "trust_proxy", "RATE_LIMIT_TRUST_PROXY");
    overrideInt(rateLimit, "max_active_tasks_per_user", "RATE_LIMIT_MAX_ACTIVE_TASKS_PER_USER");
    overrideInt(rateLimit, "user_create_capacity", "RATE_LIMIT_USER_CREATE_CAPACITY");
    overrideInt(rateLimit, "user_create_window_seconds", "RATE_LIMIT_USER_CREATE_WINDOW_SECONDS");
    overrideInt(rateLimit, "auth_ip_capacity", "RATE_LIMIT_AUTH_IP_CAPACITY");
    overrideInt(rateLimit, "auth_ip_window_seconds", "RATE_LIMIT_AUTH_IP_WINDOW_SECONDS");
    overrideString(rateLimit, "key_prefix", "RATE_LIMIT_KEY_PREFIX");

    overrideBool(cors, "enabled", "CORS_ENABLED");
    overrideStringList(cors, "allow_origins", "CORS_ALLOW_ORIGINS");

    overrideString(server, "host", "BACKEND_HOST");
    overrideInt(server, "port", "BACKEND_PORT");
    overrideInt(server, "threads", "BACKEND_THREADS");

    overrideString(database, "host", "DB_HOST");
    overrideInt(database, "port", "DB_PORT");
    overrideString(database, "username", "DB_USERNAME");
    overrideSecretString(database, "password", "DB_PASSWORD", "DB_PASSWORD_FILE");
    overrideString(database, "database", "DB_NAME");
    overrideInt(database, "pool_size", "DB_POOL_SIZE");
    overrideBool(database, "ssl", "DB_SSL");

    overrideSecretString(jwt, "secret", "JWT_SECRET", "JWT_SECRET_FILE");
    overrideInt(jwt, "access_expiration_minutes", "JWT_ACCESS_EXPIRATION_MINUTES");
    overrideInt(jwt, "refresh_expiration_days", "JWT_REFRESH_EXPIRATION_DAYS");
    if (!jwt.contains("access_expiration_minutes")) {
        if (auto legacyHours = readEnv("JWT_EXPIRATION_HOURS")) {
            try {
                jwt["access_expiration_minutes"] = (std::max)(1, std::stoi(*legacyHours) * 60);
            } catch (...) {
            }
        }
    }

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
    overrideSecretString(minio, "secret_key", "MINIO_SECRET_KEY", "MINIO_SECRET_KEY_FILE");
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
