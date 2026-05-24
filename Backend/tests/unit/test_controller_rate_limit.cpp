#include <gtest/gtest.h>

#include <chrono>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/HttpRequest.h>
#include <nlohmann/json.hpp>

#include "controllers/auth_controller.h"
#include "controllers/image_controller.h"
#include "services/rate_limiter.h"

namespace {

class FakeRateLimiter : public IRateLimiter {
  public:
    bool allow{false};
    std::vector<std::string> keys;

    std::expected<void, ServiceError> tryAcquire(std::string_view key, int,
                                                 std::chrono::seconds) override {
        keys.emplace_back(key);
        if (allow) {
            return {};
        }
        return std::unexpected(
            ServiceError::tooManyRequests("rate_limit_exceeded", "too many requests"));
    }
};

class ScopedRateLimiterOverride {
  public:
    explicit ScopedRateLimiterOverride(std::shared_ptr<IRateLimiter> limiter) {
        rate_limit::setDefaultRateLimiterForTesting(std::move(limiter));
    }

    ~ScopedRateLimiterOverride() {
        rate_limit::setDefaultRateLimiterForTesting(nullptr);
    }
};

drogon::HttpRequestPtr requestWithBody(std::string body) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(std::move(body));
    return req;
}

drogon::HttpResponsePtr invokeImageCreate(ImageController& controller,
                                          const drogon::HttpRequestPtr& req) {
    drogon::HttpResponsePtr response;
    controller.create(req, [&](const drogon::HttpResponsePtr& resp) { response = resp; });
    return response;
}

drogon::HttpResponsePtr invokeRegister(AuthController& controller,
                                       const drogon::HttpRequestPtr& req) {
    drogon::HttpResponsePtr response;
    controller.registerUser(req, [&](const drogon::HttpResponsePtr& resp) { response = resp; });
    return response;
}

drogon::HttpResponsePtr invokeLogin(AuthController& controller, const drogon::HttpRequestPtr& req) {
    drogon::HttpResponsePtr response;
    controller.login(req, [&](const drogon::HttpResponsePtr& resp) { response = resp; });
    return response;
}

nlohmann::json responseJson(const drogon::HttpResponsePtr& response) {
    return nlohmann::json::parse(std::string{response->body()});
}

} // namespace

TEST(ControllerRateLimit, ImageCreateReturns429WhenUserBucketIsExceeded) {
    auto limiter = std::make_shared<FakeRateLimiter>();
    ScopedRateLimiterOverride override(limiter);
    auto req = requestWithBody("{");
    req->attributes()->insert("userId", int64_t{42});
    req->attributes()->insert("userRole", std::string{"user"});

    ImageController controller;
    const auto response = invokeImageCreate(controller, req);

    ASSERT_TRUE(response);
    EXPECT_EQ(response->statusCode(), drogon::k429TooManyRequests);
    EXPECT_EQ(responseJson(response).at("error").at("code"), "rate_limit_exceeded");
    ASSERT_EQ(limiter->keys.size(), 1);
    EXPECT_EQ(limiter->keys.front(), "zimage:rate:user:42");
}

TEST(ControllerRateLimit, ImageCreateSkipsRateLimiterForAdmin) {
    auto limiter = std::make_shared<FakeRateLimiter>();
    ScopedRateLimiterOverride override(limiter);
    auto req = requestWithBody("{");
    req->attributes()->insert("userId", int64_t{42});
    req->attributes()->insert("userRole", std::string{"admin"});

    ImageController controller;
    const auto response = invokeImageCreate(controller, req);

    ASSERT_TRUE(response);
    EXPECT_EQ(response->statusCode(), drogon::k400BadRequest);
    EXPECT_TRUE(limiter->keys.empty());
}

TEST(ControllerRateLimit, RegisterReturns429WhenIpBucketIsExceeded) {
    auto limiter = std::make_shared<FakeRateLimiter>();
    ScopedRateLimiterOverride override(limiter);

    AuthController controller;
    const auto response = invokeRegister(controller, requestWithBody("{"));

    ASSERT_TRUE(response);
    EXPECT_EQ(response->statusCode(), drogon::k429TooManyRequests);
    EXPECT_EQ(responseJson(response).at("error").at("code"), "rate_limit_exceeded");
    ASSERT_EQ(limiter->keys.size(), 1);
    EXPECT_TRUE(limiter->keys.front().starts_with("zimage:rate:ip:"));
}

TEST(ControllerRateLimit, LoginReturns429WhenIpBucketIsExceeded) {
    auto limiter = std::make_shared<FakeRateLimiter>();
    ScopedRateLimiterOverride override(limiter);

    AuthController controller;
    const auto response = invokeLogin(controller, requestWithBody("{"));

    ASSERT_TRUE(response);
    EXPECT_EQ(response->statusCode(), drogon::k429TooManyRequests);
    EXPECT_EQ(responseJson(response).at("error").at("code"), "rate_limit_exceeded");
    ASSERT_EQ(limiter->keys.size(), 1);
    EXPECT_TRUE(limiter->keys.front().starts_with("zimage:rate:ip:"));
}
