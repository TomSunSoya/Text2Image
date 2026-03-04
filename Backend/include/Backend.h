#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace backend {

nlohmann::json loadConfig(const std::string& path = "config.json");

}
