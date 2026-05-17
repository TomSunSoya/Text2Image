#include "controllers/handler_utils.h"

#include <expected>
#include <functional>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

nlohmann::json responseBody(const drogon::HttpResponsePtr& resp) {
    return nlohmann::json::parse(std::string{resp->body()});
}

} // namespace

TEST(HandlerUtils, RespondFromExpectedSuccessUsesRenderer) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    const std::expected<int, ServiceError> result{42};

    controllers::respondFromExpected(resp, result, drogon::k200OK,
                                     [](int value) { return std::to_string(value); });

    EXPECT_EQ(drogon::k200OK, resp->statusCode());
    EXPECT_EQ("42", std::string{resp->body()});
}

TEST(HandlerUtils, RespondFromExpectedFailureWritesServiceError) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    const std::expected<int, ServiceError> result =
        std::unexpected(ServiceError{drogon::k404NotFound, "not_found", "msg"});

    controllers::respondFromExpected(resp, result, drogon::k200OK,
                                     [](int value) { return std::to_string(value); });

    EXPECT_EQ(drogon::k404NotFound, resp->statusCode());
    EXPECT_EQ("not_found", responseBody(resp).at("error").at("code"));
    EXPECT_EQ("msg", responseBody(resp).at("error").at("message"));
}

TEST(HandlerUtils, RespondFromExpectedVoidSuccessUsesNullaryRenderer) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    const std::expected<void, ServiceError> result{};

    controllers::respondFromExpected(resp, result, drogon::k200OK,
                                     [] { return std::string{R"({"ok":true})"}; });

    EXPECT_EQ(drogon::k200OK, resp->statusCode());
    EXPECT_TRUE(responseBody(resp).at("ok"));
}

TEST(HandlerUtils, RunJsonHandlerCatchesParseError) {
    drogon::HttpResponsePtr captured;

    controllers::runJsonHandler(
        [&](const drogon::HttpResponsePtr& resp) { captured = resp; }, "HandlerUtilsTest",
        [](const drogon::HttpResponsePtr&) { (void)nlohmann::json::parse("{"); });

    ASSERT_TRUE(captured);
    EXPECT_EQ(drogon::k400BadRequest, captured->statusCode());
    EXPECT_EQ("invalid_json_body", responseBody(captured).at("error").at("code"));
}
