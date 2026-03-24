#include "jwt_middleware.h"

#include <drogon/HttpResponse.h>

#include "jwt_utils.h"

void JwtMiddleware::doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& callback,
                             drogon::FilterChainCallback&& chainCallback) {
    auto token = utils::extractBearerToken(req);
    if (!token) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        resp->setBody(R"({"error":"missing bearer token"})");
        resp->setStatusCode(drogon::k401Unauthorized);
        callback(resp);
        return;
    }

    auto payload = utils::verifyToken(*token);
    if (!payload || payload->user_id <= 0) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        resp->setBody(R"({"error":"invalid token"})");
        resp->setStatusCode(drogon::k401Unauthorized);
        callback(resp);
        return;
    }

    // Pass the verified userId to downstream handlers via request attributes,
    // so controllers don't need to re-parse the JWT token.
    req->attributes()->insert("userId", payload->user_id);

    chainCallback();
}
