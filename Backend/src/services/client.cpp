#include "client.h"

#include <curl/curl.h>
#include <functional>

namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

HttpResult performRequest(const std::string& url,
                          const std::vector<std::string>& headers,
                          long timeoutSeconds,
                          const std::function<void(CURL*)>& setupCurl)
{
    HttpResult result;

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "failed to initialize curl";
        return result;
    }

    curl_slist* headerList = nullptr;
    for (const auto& header : headers) {
        headerList = curl_slist_append(headerList, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);

    setupCurl(curl);

    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    const auto code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        result.error = curl_easy_strerror(code);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status_code);
    }

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return result;
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
    return performRequest(url, headers, timeout_seconds_, [followRedirects](CURL* curl) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        if (followRedirects) {
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        }
    });
}

HttpResult HttpClient::postJson(const std::string& url,
                                const std::string& payload,
                                const std::vector<std::string>& headers) const
{
    auto allHeaders = headers;
    allHeaders.insert(allHeaders.begin(), "Content-Type: application/json");

    return performRequest(url, allHeaders, timeout_seconds_, [&payload](CURL* curl) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
    });
}
