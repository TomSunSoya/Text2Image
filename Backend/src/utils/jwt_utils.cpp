#include "jwt_utils.h"

#include <algorithm>
#include <string_view>

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

#include "Backend.h"

namespace {

using traits = jwt::traits::nlohmann_json;

std::string loadSecret()
{
    try {
        const auto config = backend::loadConfig();
        return config.at("jwt").at("secret").get<std::string>();
    } catch (...) {
        return "development-secret-change-me";
    }
}

std::string loadIssuer()
{
    return "backend";
}

}

namespace utils {

std::string createToken(int64_t userId, const std::string& username)
{
    return jwt::create<jwt::default_clock, traits>(jwt::default_clock{})
        .set_issuer(loadIssuer())
        .set_payload_claim("uid", traits::value_type(static_cast<traits::integer_type>(userId)))
        .set_payload_claim("username", traits::value_type(username))
        .sign(jwt::algorithm::hs256{loadSecret()});
}

std::optional<JwtPayload> verifyToken(const std::string& token)
{
    try {
        auto decoded = jwt::decode<traits>(token);

        auto verifier = jwt::verify<jwt::default_clock, traits>(jwt::default_clock{})
            .allow_algorithm(jwt::algorithm::hs256{loadSecret()})
            .with_issuer(loadIssuer());

        verifier.verify(decoded);

        const auto uidClaim = decoded.get_payload_claim("uid");

        JwtPayload payload;
        try {
            payload.user_id = static_cast<int64_t>(uidClaim.as_integer());
        } catch (...) {
            // Backward-compatible path for tokens that encode uid as float.
            payload.user_id = static_cast<int64_t>(uidClaim.as_number());
        }
        payload.username = decoded.get_payload_claim("username").as_string();
        return payload;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> extractBearerToken(const drogon::HttpRequestPtr& req)
{
    const auto header = req->getHeader("Authorization");
    constexpr std::string_view prefix = "Bearer ";

    if (header.size() <= prefix.size()) {
        return std::nullopt;
    }

    if (!std::equal(prefix.begin(), prefix.end(), header.begin())) {
        return std::nullopt;
    }

    return header.substr(prefix.size());
}

}
