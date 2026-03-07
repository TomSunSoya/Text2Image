#include "Backend.h"

#include <fstream>
#include <filesystem>
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

}

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
        return config;
    }

    std::string message = "failed to open config file: " + path + " (tried:";
    for (const auto& p : tried) {
        message += " " + p.string();
    }
    message += ")";
    throw std::runtime_error(message);
}

}
