#pragma once

#include <string>

namespace utils {

std::string encodeToBase64(const std::string& bytes);
std::string decodeBase64(const std::string& base64);

} // namespace utils
