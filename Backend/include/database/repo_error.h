#pragma once

#include <expected>
#include <string>
#include <utility>

struct RepoError {
    enum class Kind {
        DbUnavailable,
        QueryFailed,
        ConstraintViolation,
        Serialization,
        Internal,
    };

    Kind kind{Kind::Internal};
    std::string message;

    RepoError() = default;
    RepoError(Kind k, std::string m) : kind(k), message(std::move(m)) {}
};

template <typename T> using RepoResult = std::expected<T, RepoError>;
