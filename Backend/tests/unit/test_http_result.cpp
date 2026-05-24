#include <gtest/gtest.h>

#include "services/i_http_client.h"

TEST(HttpResult, ToExpectedBodyReturnsBodyForSuccessfulStatus) {
    const HttpResult result{200, "payload"};

    const auto body = result.toExpectedBody();

    ASSERT_TRUE(body.has_value());
    EXPECT_EQ(*body, "payload");
}

TEST(HttpResult, ToExpectedBodyReturnsErrorStringForRequestError) {
    HttpResult result;
    result.failure = HttpError{0, "request failed"};

    const auto body = result.toExpectedBody();

    ASSERT_FALSE(body.has_value());
    EXPECT_EQ(body.error().status_code, 0);
    EXPECT_EQ(body.error().message, "request failed");
}

TEST(HttpResult, ToExpectedBodyReturnsStatusFallbackForHttpError) {
    const HttpResult result{500, "body"};

    const auto body = result.toExpectedBody();

    ASSERT_FALSE(body.has_value());
    EXPECT_EQ(body.error().status_code, 500);
    EXPECT_EQ(body.error().message, "http status 500");
}
