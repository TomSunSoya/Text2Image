#include "client.h"

#include <cctype>
#include <chrono>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <trantor/net/EventLoopThread.h>

namespace {

struct ParsedUrl {
    std::string origin;
    std::string target;
};

struct SendEnvelope {
    HttpResult http;
    drogon::HttpResponsePtr response;
};

std::string trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::optional<ParsedUrl> parseUrl(const std::string& url)
{
    const auto schemePos = url.find("://");
    if (schemePos == std::string::npos) {
        return std::nullopt;
    }

    const auto authorityStart = schemePos + 3;
    const auto pathStart = url.find('/', authorityStart);

    ParsedUrl parsed;
    if (pathStart == std::string::npos) {
        parsed.origin = url;
        parsed.target = "/";
    } else {
        parsed.origin = url.substr(0, pathStart);
        parsed.target = url.substr(pathStart);
        if (parsed.target.empty()) {
            parsed.target = "/";
        }
    }

    if (parsed.origin.empty()) {
        return std::nullopt;
    }

    return parsed;
}

void applyHeaders(const drogon::HttpRequestPtr& req, const std::vector<std::string>& headers)
{
    for (const auto& rawHeader : headers) {
        const auto split = rawHeader.find(':');
        if (split == std::string::npos) {
            continue;
        }

        auto key = trim(rawHeader.substr(0, split));
        auto value = trim(rawHeader.substr(split + 1));
        if (!key.empty()) {
            req->addHeader(key, value);
        }
    }
}

bool isRedirectStatus(long statusCode)
{
    return statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308;
}

std::string resolveRedirectUrl(const ParsedUrl& current, const std::string& location)
{
    if (location.empty()) {
        return {};
    }

    if (location.starts_with("http://") || location.starts_with("https://")) {
        return location;
    }

    if (location.front() == '/') {
        return current.origin + location;
    }

    return current.origin + "/" + location;
}

trantor::EventLoop* sharedHttpClientLoop()
{
    static trantor::EventLoopThread loopThread("backend-http-client-loop");
    static std::once_flag startOnce;

    std::call_once(startOnce, [&] {
        loopThread.run();
    });

    auto* loop = loopThread.getLoop();
    while (loop == nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        loop = loopThread.getLoop();
    }

    return loop;
}

SendEnvelope sendOnce(const ParsedUrl& parsed,
                      drogon::HttpMethod method,
                      const std::string* payload,
                      const std::vector<std::string>& headers,
                      long timeoutSeconds)
{
    SendEnvelope envelope{};

    auto client = drogon::HttpClient::newHttpClient(parsed.origin, sharedHttpClientLoop());
    if (!client) {
        envelope.http.error = "failed to initialize drogon http client";
        return envelope;
    }

    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(method);
    request->setPath(parsed.target);

    if (payload) {
        request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        request->setBody(*payload);
    }

    applyHeaders(request, headers);

    const double timeout = timeoutSeconds > 0 ? static_cast<double>(timeoutSeconds) : 0.0;

    try {
        auto [result, response] = client->sendRequest(request, timeout);
        if (result != drogon::ReqResult::Ok) {
            envelope.http.error = std::format("request failed, req_result={}", static_cast<int>(result));
            return envelope; 
        }

        envelope.response = std::move(response);
    } catch (const std::exception& ex) {
        envelope.http.error = std::format("request exception: {}", ex.what());
        return envelope;
    }

    if (!envelope.response) {
        envelope.http.error = "request failed, empty response";
        return envelope;
    }

    envelope.http.status_code = static_cast<long>(envelope.response->statusCode());
    envelope.http.body = envelope.response->body();
    return envelope;
}

} // namespace

bool HttpResult::ok() const
{
    return error.empty() && status_code >= 200 && status_code < 300;
}

HttpClient::HttpClient(long timeoutSeconds) : timeout_seconds_(timeoutSeconds)
{
}

HttpResult HttpClient::get(const std::string& url,
                           const std::vector<std::string>& headers,
                           bool followRedirects) const
{
    constexpr int kMaxRedirects = 5;
    std::string currentUrl = url;

    for (int redirect = 0;; ++redirect) {
        const auto parsed = parseUrl(currentUrl);
        if (!parsed) {
            HttpResult invalid;
            invalid.error = std::format("invalid url: {}", currentUrl);
            return invalid;
        }

        auto envelope = sendOnce(*parsed, drogon::Get, nullptr, headers, timeout_seconds_);

        if (!followRedirects || !envelope.response) {
            return envelope.http;
        }

        if (!isRedirectStatus(envelope.http.status_code)) {
            return envelope.http;
        }

        if (redirect >= kMaxRedirects) {
            envelope.http.error = "too many redirects";
            return envelope.http;
        }

        const auto location = envelope.response->getHeader("Location");
        currentUrl = resolveRedirectUrl(*parsed, location);
        if (currentUrl.empty()) {
            envelope.http.error = "redirect response missing Location header";
            return envelope.http;
        }
    }
}

HttpResult HttpClient::postJson(const std::string& url,
                                const std::string& payload,
                                const std::vector<std::string>& headers) const
{
    const auto parsed = parseUrl(url);
    if (!parsed) {
        HttpResult invalid;
        invalid.error = std::format("invalid url: {}", url);
        return invalid;
    }

    auto envelope = sendOnce(*parsed, drogon::Post, &payload, headers, timeout_seconds_);
    return envelope.http;
}
