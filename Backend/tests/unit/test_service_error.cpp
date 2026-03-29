#include <gtest/gtest.h>
#include "services/service_error.h"

TEST(ServiceError, ToJsonBasic) {
    ServiceError err{drogon::k400BadRequest, "bad_input", "input is invalid"};
    auto j = err.toJson();
    EXPECT_EQ(j.at("code"), "bad_input");
    EXPECT_EQ(j.at("message"), "input is invalid");
    EXPECT_FALSE(j.contains("details")); // empty details omitted
}

TEST(ServiceError, ToJsonWithDetails) {
    ServiceError err{drogon::k409Conflict, "conflict", "state mismatch"};
    err.details["status"] = "success";
    err.details["retryCount"] = 3;

    auto j = err.toJson();
    EXPECT_TRUE(j.contains("details"));
    EXPECT_EQ(j.at("details").at("status"), "success");
    EXPECT_EQ(j.at("details").at("retryCount"), 3);
}

TEST(ServiceError, DefaultConstructor) {
    ServiceError err;
    EXPECT_EQ(err.status, drogon::k500InternalServerError);
    EXPECT_EQ(err.code, "internal_error");
    EXPECT_EQ(err.message, "internal_error");
}

TEST(ServiceError, EmptyDetailsObjectOmitted) {
    ServiceError err{drogon::k404NotFound, "not_found", "resource not found"};
    err.details = nlohmann::json::object(); // explicitly empty
    auto j = err.toJson();
    EXPECT_FALSE(j.contains("details"));
}
