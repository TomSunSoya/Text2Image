#pragma once

#include "database/repo_error.h"
#include "services/service_error.h"

[[nodiscard]] ServiceError mapRepoError(const RepoError& error);
