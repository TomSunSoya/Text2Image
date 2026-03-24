#include "base64_utils.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

#include <openssl/evp.h>

namespace utils {

std::string encodeToBase64(const std::string& bytes) {
    if (bytes.empty()) {
        return {};
    }

    if (bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return {};
    }

    std::string base64((bytes.size() + 2) / 3 * 4, '\0');
    const int outLen = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&base64[0]),
                                       reinterpret_cast<const unsigned char*>(bytes.data()),
                                       static_cast<int>(bytes.size()));

    if (outLen <= 0) {
        return {};
    }

    base64.resize(static_cast<size_t>(outLen));
    return base64;
}

std::string decodeBase64(const std::string& input) {
    std::string base64 = input;
    base64.erase(std::remove_if(base64.begin(), base64.end(),
                                [](unsigned char c) { return std::isspace(c); }),
                 base64.end());

    std::string out(base64.size() / 4 * 3, '\0');

    const int decoded = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(&out[0]),
                                        reinterpret_cast<const unsigned char*>(base64.data()),
                                        static_cast<int>(base64.size()));

    if (decoded < 0) {
        throw std::runtime_error("invalid base64 payload");
    }

    int padding = 0;
    if (!base64.empty() && base64.back() == '=')
        ++padding;
    if (base64.size() > 1 && base64[base64.size() - 2] == '=')
        ++padding;

    out.resize(static_cast<size_t>(decoded - padding));
    return out;
}

} // namespace utils
