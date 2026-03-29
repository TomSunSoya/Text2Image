#pragma once

#include <drogon/HttpFilter.h>

class JwtMiddleware : public drogon::HttpFilter<JwtMiddleware> {
  public:
    void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& callback,
                  drogon::FilterChainCallback&& chainCallback) override;
};
