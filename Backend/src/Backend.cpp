#include "Backend.h"

#include <fstream>
#include <stdexcept>

namespace backend {

nlohmann::json loadConfig(const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open config file: " + path);
    }

    nlohmann::json config;
    input >> config;
    return config;
}

}
