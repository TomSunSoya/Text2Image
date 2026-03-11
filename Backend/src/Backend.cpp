#include "Backend.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace backend {

namespace {

std::filesystem::path executableDir()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const auto len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer).parent_path();
#else
    return {};
#endif
}

std::vector<std::filesystem::path> buildCandidatePaths(const std::string& path)
{
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path input(path);

    candidates.push_back(input);

    if (input.is_relative()) {
        candidates.push_back(std::filesystem::current_path() / input);

        const auto exeDir = executableDir();
        if (!exeDir.empty()) {
            candidates.push_back(exeDir / input);

            auto walk = exeDir;
            for (int i = 0; i < 8 && walk.has_parent_path(); ++i) {
                walk = walk.parent_path();
                candidates.push_back(walk / input);
            }
        }
    }

    return candidates;
}

std::optional<std::string> readEnv(const char* name)
{
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

void overrideString(nlohmann::json& object, const char* key, const char* envName)
{
    if (auto value = readEnv(envName)) {
        object[key] = *value;
    }
}

void overrideInt(nlohmann::json& object, const char* key, const char* envName)
{
    if (auto value = readEnv(envName)) {
        try {
            object[key] = std::stoi(*value);
        } catch (...) {
        }
    }
}

void applyEnvOverrides(nlohmann::json& config)
{
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

    overrideString(server, "host", "BACKEND_HOST");
    overrideInt(server, "port", "BACKEND_PORT");
    overrideInt(server, "threads", "BACKEND_THREADS");

    overrideString(database, "host", "DB_HOST");
    overrideInt(database, "port", "DB_PORT");
    overrideString(database, "username", "DB_USERNAME");
    overrideString(database, "password", "DB_PASSWORD");
    overrideString(database, "database", "DB_NAME");

    overrideString(jwt, "secret", "JWT_SECRET");
    overrideInt(jwt, "expiration_hours", "JWT_EXPIRATION_HOURS");

    overrideString(pythonService, "url", "PYTHON_SERVICE_URL");
    overrideInt(pythonService, "timeout_seconds", "PYTHON_SERVICE_TIMEOUT_SECONDS");
    overrideInt(pythonService, "queue_workers", "PYTHON_SERVICE_QUEUE_WORKERS");
}

} // namespace

nlohmann::json loadConfig(const std::string& path)
{
    std::vector<std::filesystem::path> tried;
    for (const auto& candidate : buildCandidatePaths(path)) {
        tried.push_back(candidate);

        std::ifstream input(candidate);
        if (!input.is_open()) {
            continue;
        }

        nlohmann::json config;
        input >> config;
        applyEnvOverrides(config);
        return config;
    }

    std::string message = "failed to open config file: " + path + " (tried:";
    for (const auto& p : tried) {
        message += " " + p.string();
    }
    message += ")";
    throw std::runtime_error(message);
}

} // namespace backend


