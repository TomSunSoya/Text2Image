#pragma once

#include <string_view>

namespace models::failure {

inline constexpr std::string_view kPythonServiceRequestFailed = "python_service_request_failed";
inline constexpr std::string_view kPythonServiceEmptyResponse = "python_service_empty_response";
inline constexpr std::string_view kPythonServiceInvalidJson = "python_service_invalid_json";
inline constexpr std::string_view kPythonServiceException = "python_service_exception";
inline constexpr std::string_view kPythonServiceUnknownException =
    "python_service_unknown_exception";
inline constexpr std::string_view kMissingImagePayload = "missing_image_payload";
inline constexpr std::string_view kStorageWriteFailed = "storage_write_failed";
inline constexpr std::string_view kLeaseExpired = "lease_expired";
inline constexpr std::string_view kLeaseExpiredMaxRetries = "lease_expired_max_retries";

} // namespace models::failure
