#pragma once

#include <string>
#include <vector>

struct HttpResult {
    long status_code{0};
    std::string body;
    std::string error;

    bool ok() const;
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
