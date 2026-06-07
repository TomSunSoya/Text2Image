#include <gtest/gtest.h>

#include "services/http_error_mapper.h"

TEST(HttpErrorMapper, MapsNetworkErrorToServiceUnavailable) {
    const auto error = mapHttpError(HttpError{0, "connection refused"});

    EXPECT_EQ(error.status, drogon::k503ServiceUnavailable);
    EXPECT_EQ(error.code, "model_service_unavailable");
    EXPECT_EQ(error.message, "model service is unavailable");
}

TEST(HttpErrorMapper, MapsServerErrorToBadGateway) {
    const auto error = mapHttpError(HttpError{500, "http status 500"});

    EXPECT_EQ(error.status, drogon::k502BadGateway);
    EXPECT_EQ(error.code, "model_service_error");
    EXPECT_EQ(error.message, "model service returned an error");
}

TEST(HttpErrorMapper, MapsClientErrorToBadGateway) {
    const auto error = mapHttpError(HttpError{400, "http status 400"});

    EXPECT_EQ(error.status, drogon::k502BadGateway);
    EXPECT_EQ(error.code, "model_service_error");
    EXPECT_EQ(error.message, "model service rejected the request");
}
