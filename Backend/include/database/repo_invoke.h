#pragma once

#include <exception>
#include <expected>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <mysqlx/xdevapi.h>

#include "database/repo_error.h"

class RepoSerializationError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Run a repository operation and translate any escaping exception into a RepoError.
// Kept in a header (not split across each Repo .cpp) so error classification stays
// in one place — adding a new catch arm only requires editing this file.
template <typename Fn> auto repoInvoke(Fn&& fn) -> RepoResult<std::invoke_result_t<Fn>> {
    try {
        return std::forward<Fn>(fn)();
    } catch (const RepoSerializationError& ex) {
        return std::unexpected(RepoError{RepoError::Kind::Serialization, ex.what()});
    } catch (const mysqlx::Error& ex) {
        return std::unexpected(makeRepoErrorFromMysqlMessage(ex.what()));
    } catch (const std::exception& ex) {
        return std::unexpected(makeRepoErrorFromExceptionMessage(ex.what()));
    } catch (...) {
        return std::unexpected(RepoError{RepoError::Kind::Internal, "unknown repository error"});
    }
}
