#include "services/repo_error_mapper.h"

#include <string_view>

#include <spdlog/spdlog.h>

namespace {

std::string_view kindName(RepoError::Kind kind) {
    switch (kind) {
        case RepoError::Kind::DbUnavailable:
            return "DbUnavailable";
        case RepoError::Kind::QueryFailed:
            return "QueryFailed";
        case RepoError::Kind::ConstraintViolation:
            return "ConstraintViolation";
        case RepoError::Kind::Serialization:
            return "Serialization";
        case RepoError::Kind::Internal:
            return "Internal";
    }

    return "Internal";
}

} // namespace

ServiceError mapRepoError(const RepoError& error) {
    spdlog::error("repository error kind={}, message={}", kindName(error.kind), error.message);

    switch (error.kind) {
        case RepoError::Kind::DbUnavailable:
            return ServiceError{drogon::k503ServiceUnavailable, "database_unavailable",
                                "database is unavailable"};
        case RepoError::Kind::ConstraintViolation:
            return ServiceError{drogon::k409Conflict, "database_constraint_violation",
                                "database constraint violation"};
        case RepoError::Kind::Serialization:
            return ServiceError{drogon::k500InternalServerError, "database_serialization_failed",
                                "failed to read database row"};
        case RepoError::Kind::QueryFailed:
            return ServiceError{drogon::k500InternalServerError, "database_query_failed",
                                "database query failed"};
        case RepoError::Kind::Internal:
            return ServiceError{drogon::k500InternalServerError, "database_internal_error",
                                "database operation failed"};
    }

    return ServiceError{drogon::k500InternalServerError, "database_internal_error",
                        "database operation failed"};
}
