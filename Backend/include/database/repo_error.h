#pragma once

#include <expected>
#include <string>
#include <string_view>
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

[[nodiscard]] RepoError makeRepoErrorFromMysqlMessage(std::string_view message);
[[nodiscard]] RepoError makeRepoErrorFromExceptionMessage(std::string_view message);
