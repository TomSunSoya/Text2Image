#pragma once

#include <string>

namespace security {
std::string hashPassword(const std::string &plain);
bool verifyPassword(const std::string &plain, const std::string &stored);
}