#pragma once

#include "services/i_http_client.h"
#include "services/service_error.h"

[[nodiscard]] ServiceError mapHttpError(const HttpError& error);
