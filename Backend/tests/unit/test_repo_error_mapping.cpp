#include <gtest/gtest.h>

#include "services/repo_error_mapper.h"

TEST(RepoErrorMapping, DbUnavailableMapsToServiceUnavailable) {
    const auto error = mapRepoError({RepoError::Kind::DbUnavailable, "mysql connection refused"});

    EXPECT_EQ(error.status, drogon::k503ServiceUnavailable);
    EXPECT_EQ(error.code, "database_unavailable");
}

TEST(RepoErrorMapping, QueryFailedMapsToInternalServerError) {
    const auto error = mapRepoError({RepoError::Kind::QueryFailed, "syntax error"});

    EXPECT_EQ(error.status, drogon::k500InternalServerError);
    EXPECT_EQ(error.code, "database_query_failed");
}

TEST(RepoErrorMapping, ConstraintViolationMapsToConflict) {
    const auto error = mapRepoError({RepoError::Kind::ConstraintViolation, "duplicate key"});

    EXPECT_EQ(error.status, drogon::k409Conflict);
    EXPECT_EQ(error.code, "database_constraint_violation");
}

TEST(RepoErrorMapping, SerializationMapsToInternalServerError) {
    const auto error = mapRepoError({RepoError::Kind::Serialization, "bad row"});

    EXPECT_EQ(error.status, drogon::k500InternalServerError);
    EXPECT_EQ(error.code, "database_serialization_failed");
}

TEST(RepoErrorMapping, InternalMapsToInternalServerError) {
    const auto error = mapRepoError({RepoError::Kind::Internal, "unknown"});

    EXPECT_EQ(error.status, drogon::k500InternalServerError);
    EXPECT_EQ(error.code, "database_internal_error");
}
