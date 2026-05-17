#pragma once

#include <expected>
#include <string>
#include <vector>

struct HttpError {
    long status_code{0};
    std::string message;
};

struct HttpResult {
    long status_code{0};
    std::string body;
    std::string error;

    bool ok() const;
    [[nodiscard]] std::expected<std::string, HttpError> toExpectedBody() const;
};

class IHttpClient {
  public:
    virtual ~IHttpClient() = default;

    virtual HttpResult get(const std::string& url, long timeoutSeconds,
                           const std::vector<std::string>& headers = {},
                           bool followRedirects = false) const = 0;

    virtual HttpResult postJson(const std::string& url, long timeoutSeconds,
                                const std::string& payload,
                                const std::vector<std::string>& headers = {}) const = 0;
};
