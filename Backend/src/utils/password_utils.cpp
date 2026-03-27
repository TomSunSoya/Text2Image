#include "password_utils.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kSaltBytes = 16;      // 128 bits
constexpr int kHashBytes = 32;      // 256 bits
constexpr int kIterations = 100000; // Number of iterations for PBKDF2

std::string toHex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::vector<unsigned char> fromHex(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        throw std::invalid_argument("Invalid hex string");
    }

    std::vector<unsigned char> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        unsigned int byte = 0;
        std::istringstream iss(hex.substr(i, 2));
        iss >> std::hex >> byte;
        if (iss.fail()) {
            throw std::invalid_argument("Invalid hex string");
        }
        bytes.push_back(static_cast<unsigned char>(byte));
    }
    return bytes;
}

std::vector<std::string> split(const std::string& str, char delim) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : str) {
        if (c == delim) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(cur);
    return parts;
}
} // namespace

std::string security::hashPassword(const std::string& plain) {
    unsigned char salt[kSaltBytes];
    if (RAND_bytes(salt, kSaltBytes) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }

    unsigned char out[kHashBytes];
    if (PKCS5_PBKDF2_HMAC(plain.c_str(), static_cast<int>(plain.size()), salt, kSaltBytes,
                          kIterations, EVP_sha256(), kHashBytes, out) != 1) {
        throw std::runtime_error("PKCS5_PBKDF2_HMAC failed");
    }

    return "pbkdf2_sha256$" + std::to_string(kIterations) + "$" + toHex(salt, kSaltBytes) + "$" +
           toHex(out, kHashBytes);
}

bool security::verifyPassword(const std::string& plain, const std::string& stored) {
    auto parts = split(stored, '$');
    if (parts.size() != 4)
        return false;
    if (parts[0] != "pbkdf2_sha256")
        return false;

    int iter = 0;
    try {
        iter = std::stoi(parts[1]);
    } catch (...) {
        return false;
    }

    auto salt = fromHex(parts[2]);
    auto hash = fromHex(parts[3]);
    if (salt.empty() || hash.empty())
        return false;

    std::vector<unsigned char> out(hash.size());
    if (PKCS5_PBKDF2_HMAC(plain.c_str(), static_cast<int>(plain.size()), salt.data(),
                          static_cast<int>(salt.size()), iter, EVP_sha256(),
                          static_cast<int>(out.size()), out.data()) != 1) {
        return false;
    }

    // 常量时间比较
    if (out.size() != hash.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        diff |= (out[i] ^ hash[i]);
    }
    return diff == 0;
}
