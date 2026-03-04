#include "client.h"

#include <curl/curl.h>

namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

}

bool HttpResult::ok() const
{
    return error.empty() && status_code >= 200 && status_code < 300;
}

HttpClient::HttpClient(long timeoutSeconds) : timeout_seconds_(timeoutSeconds)
{
}

HttpResult HttpClient::postJson(const std::string& url,
                                const std::string& payload,
                                const std::vector<std::string>& headers) const
{
    HttpResult result;

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "failed to initialize curl";
        return result;
    }

    curl_slist* headerList = nullptr;
    headerList = curl_slist_append(headerList, "Content-Type: application/json");
    for (const auto& header : headers) {
        headerList = curl_slist_append(headerList, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds_);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);

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
