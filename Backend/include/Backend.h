#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace backend {

nlohmann::json loadConfig(const std::string& path = "config.json");

/// Returns a reference to the config loaded once at first call.
/// All runtime reads should use this instead of loadConfig() to avoid
/// repeated file I/O and JSON parsing on every request.
const nlohmann::json& cachedConfig();

}
