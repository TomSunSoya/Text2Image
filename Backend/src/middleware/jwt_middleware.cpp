#include "jwt_middleware.h"

#include <drogon/HttpResponse.h>

#include "jwt_utils.h"

void JwtMiddleware::doFilter(const drogon::HttpRequestPtr& req,
                             drogon::FilterCallback&& callback,
                             drogon::FilterChainCallback&& chainCallback)
{
    auto token = utils::extractBearerToken(req);
    if (!token) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        resp->setBody(R"({"error":"missing bearer token"})");
        resp->setStatusCode(drogon::k401Unauthorized);
        callback(resp);
        return;
    }

    if (!utils::verifyToken(*token)) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        resp->setBody(R"({"error":"invalid token"})");
        resp->setStatusCode(drogon::k401Unauthorized);
        callback(resp);
        return;
    }

    chainCallback();
}
