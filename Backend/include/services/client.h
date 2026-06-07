#pragma once

#include <string>
#include <vector>

#include "services/i_http_client.h"

class HttpClient : public IHttpClient {
  public:
    HttpClient() = default;

    HttpResult get(const std::string& url, long timeoutSeconds,
                   const std::vector<std::string>& headers = {},
                   bool followRedirects = false) const override;

    HttpResult postJson(const std::string& url, long timeoutSeconds, const std::string& payload,
                        const std::vector<std::string>& headers = {}) const override;
};
