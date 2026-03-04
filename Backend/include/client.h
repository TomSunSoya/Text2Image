#pragma once

#include <string>
#include <vector>

struct HttpResult {
    long status_code{0};
    std::string body;
    std::string error;

    bool ok() const;
};

class HttpClient {
public:
    explicit HttpClient(long timeoutSeconds = 120);

    HttpResult postJson(const std::string& url,
                        const std::string& payload,
                        const std::vector<std::string>& headers = {}) const;

private:
    long timeout_seconds_{120};
};
