#include "database/repo_error.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace {

std::string toLowerAscii(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool containsInsensitive(std::string_view value, std::string_view needle) {
    return toLowerAscii(std::string{value}).contains(toLowerAscii(std::string{needle}));
}

RepoError::Kind classifyErrorMessage(std::string_view message) {
    if (containsInsensitive(message, "db not initialized") ||
        containsInsensitive(message, "database name is empty")) {
        return RepoError::Kind::Internal;
    }

    if (containsInsensitive(message, "can't connect") ||
        containsInsensitive(message, "cannot connect") ||
        containsInsensitive(message, "connection refused") ||
        containsInsensitive(message, "lost connection") ||
        containsInsensitive(message, "server has gone away") ||
        containsInsensitive(message, "not connected")) {
        return RepoError::Kind::DbUnavailable;
    }

    if (containsInsensitive(message, "duplicate entry") ||
        containsInsensitive(message, "foreign key constraint fails")) {
        return RepoError::Kind::ConstraintViolation;
    }

    return RepoError::Kind::QueryFailed;
}

} // namespace

RepoError makeRepoErrorFromMysqlMessage(std::string_view message) {
    return RepoError{classifyErrorMessage(message), std::string{message}};
}

RepoError makeRepoErrorFromExceptionMessage(std::string_view message) {
    const auto kind = classifyErrorMessage(message);
    return RepoError{kind == RepoError::Kind::QueryFailed ? RepoError::Kind::Internal : kind,
                     std::string{message}};
}
