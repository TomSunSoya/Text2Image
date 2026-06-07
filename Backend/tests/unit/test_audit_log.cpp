#include <gtest/gtest.h>

#include <string>

#include "utils/audit_log.h"

TEST(AuditLog, BuildsStructuredJsonWithoutSensitivePayloadFields) {
    const audit::Event event{
        .event = "auth.login",
        .outcome = "failure",
        .request_id = "trace-123",
        .client_ip = "127.0.0.1",
        .resource_id = "image:99",
        .user_id = 42,
        .status_code = 401,
    };

    const auto body = audit::toJson(event);

    EXPECT_EQ(body.at("type"), "audit");
    EXPECT_EQ(body.at("event"), "auth.login");
    EXPECT_EQ(body.at("outcome"), "failure");
    EXPECT_EQ(body.at("request_id"), "trace-123");
    EXPECT_EQ(body.at("client_ip"), "127.0.0.1");
    EXPECT_EQ(body.at("resource_id"), "image:99");
    EXPECT_EQ(body.at("user_id"), 42);
    EXPECT_EQ(body.at("status"), 401);
    EXPECT_TRUE(body.at("timestamp").is_string());
    EXPECT_FALSE(body.contains("token"));
    EXPECT_FALSE(body.contains("password"));
    EXPECT_FALSE(body.contains("prompt"));
}

TEST(AuditLog, ReplacesControlsAndBoundsLogValues) {
    const std::string value = std::string("abc\r\n") + std::string(160, 'x');

    const auto safe = audit::safeLogValue(value);

    EXPECT_EQ(safe.substr(0, 5), "abc__");
    EXPECT_EQ(safe.size(), 128u);
}

TEST(AuditLog, UsesDashForEmptyValues) {
    EXPECT_EQ(audit::safeLogValue(""), "-");
}
