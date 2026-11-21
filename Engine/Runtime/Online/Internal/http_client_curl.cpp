// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "http_client.h"

#if !defined(_WIN32) && !defined(__ANDROID__)

#include <cstdio>
#include <fstream>
#include <mutex>

#include <curl/curl.h>

namespace UVE::Online {
namespace {

/// The most redirects a request follows (release downloads redirect once, to storage).
inline constexpr long kMaximumRedirectsUVE = 5L;
/// Below this many bytes per second for the stall timeout, a transfer counts as stalled.
inline constexpr long kStallBytesPerSecondUVE = 1L;
inline constexpr long kConnectTimeoutSecondsUVE = 15L;

void EnsureGlobalInitUVE() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct DownloadSinkUVE final {
    std::ofstream* file = nullptr;
    std::string* errorBody = nullptr;
    CURL* handle = nullptr;
    bool failed = false;
};

std::size_t AppendToStringUVE(char* data, const std::size_t size, const std::size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

std::size_t WriteDownloadUVE(char* data, const std::size_t size, const std::size_t count, void* user) {
    auto* sink = static_cast<DownloadSinkUVE*>(user);
    long status = 0;
    curl_easy_getinfo(sink->handle, CURLINFO_RESPONSE_CODE, &status);
    // An error page is kept for the message rather than written as the file.
    if (!IsHttpSuccessStatusUVE(status)) {
        sink->errorBody->append(data, size * count);
        return size * count;
    }
    sink->file->write(data, static_cast<std::streamsize>(size * count));
    if (!*sink->file) {
        sink->failed = true;
        return 0U;
    }
    return size * count;
}

int ReportProgressUVE(void* user, const curl_off_t total, const curl_off_t received, curl_off_t, curl_off_t) {
    const auto* progress = static_cast<const HttpProgressUVE*>(user);
    if (progress != nullptr && *progress &&
        !(*progress)(static_cast<std::uint64_t>(received), static_cast<std::uint64_t>(total))) {
        return 1; // non-zero aborts the transfer
    }
    return 0;
}

/// One configured easy handle with its header list, released together.
class CurlRequestUVE final {
public:
    explicit CurlRequestUVE(const HttpRequestUVE& request) : m_handle(curl_easy_init()) {
        if (m_handle == nullptr) {
            return;
        }
        for (const HttpHeaderUVE& header : request.headers) {
            m_headers = curl_slist_append(m_headers, (header.name + ": " + header.value).c_str());
        }
        curl_easy_setopt(m_handle, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, m_headers);
        curl_easy_setopt(m_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(m_handle, CURLOPT_MAXREDIRS, kMaximumRedirectsUVE);
        // Never hand credentials to the host a redirect points at.
        curl_easy_setopt(m_handle, CURLOPT_UNRESTRICTED_AUTH, 0L);
        curl_easy_setopt(m_handle, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(m_handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        curl_easy_setopt(m_handle, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSecondsUVE);
        curl_easy_setopt(m_handle, CURLOPT_LOW_SPEED_LIMIT, kStallBytesPerSecondUVE);
        curl_easy_setopt(m_handle, CURLOPT_LOW_SPEED_TIME, static_cast<long>(request.stallTimeout.count()));
        curl_easy_setopt(m_handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(m_handle, CURLOPT_ERRORBUFFER, m_error);
    }
    CurlRequestUVE(const CurlRequestUVE&) = delete;
    CurlRequestUVE& operator=(const CurlRequestUVE&) = delete;
    ~CurlRequestUVE() {
        curl_slist_free_all(m_headers);
        if (m_handle != nullptr) {
            curl_easy_cleanup(m_handle);
        }
    }

    [[nodiscard]] CURL* HandleUVE() const noexcept { return m_handle; }

    [[nodiscard]] HttpResponseUVE PerformUVE() {
        HttpResponseUVE response;
        const CURLcode code = curl_easy_perform(m_handle);
        if (code != CURLE_OK) {
            response.error = m_error[0] != '\0' ? std::string(m_error) : std::string(curl_easy_strerror(code));
            return response;
        }
        response.reached = true;
        curl_easy_getinfo(m_handle, CURLINFO_RESPONSE_CODE, &response.status);
        return response;
    }

private:
    CURL* m_handle = nullptr;
    curl_slist* m_headers = nullptr;
    char m_error[CURL_ERROR_SIZE] = {};
};

class CurlHttpClientUVE final : public IHttpClientUVE {
public:
    CurlHttpClientUVE() { EnsureGlobalInitUVE(); }

    HttpResponseUVE GetUVE(const HttpRequestUVE& request) override {
        CurlRequestUVE curl(request);
        if (curl.HandleUVE() == nullptr) {
            return {false, 0, {}, "Unable to start an HTTPS request."};
        }
        std::string body;
        curl_easy_setopt(curl.HandleUVE(), CURLOPT_WRITEFUNCTION, &AppendToStringUVE);
        curl_easy_setopt(curl.HandleUVE(), CURLOPT_WRITEDATA, &body);
        HttpResponseUVE response = curl.PerformUVE();
        response.body = std::move(body);
        return response;
    }

    HttpResponseUVE DownloadUVE(const HttpRequestUVE& request, const std::filesystem::path& file,
                                const HttpProgressUVE& progress) override {
        CurlRequestUVE curl(request);
        if (curl.HandleUVE() == nullptr) {
            return {false, 0, {}, "Unable to start an HTTPS request."};
        }
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return {false, 0, {}, "Unable to write " + file.string()};
        }
        std::string errorBody;
        DownloadSinkUVE sink{&output, &errorBody, curl.HandleUVE(), false};
        curl_easy_setopt(curl.HandleUVE(), CURLOPT_WRITEFUNCTION, &WriteDownloadUVE);
        curl_easy_setopt(curl.HandleUVE(), CURLOPT_WRITEDATA, &sink);
        curl_easy_setopt(curl.HandleUVE(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.HandleUVE(), CURLOPT_XFERINFOFUNCTION, &ReportProgressUVE);
        curl_easy_setopt(curl.HandleUVE(), CURLOPT_XFERINFODATA, &progress);
        HttpResponseUVE response = curl.PerformUVE();
        output.close();
        if (sink.failed) {
            response.reached = false;
            response.error = "Unable to write " + file.string();
        }
        response.body = std::move(errorBody);
        return response;
    }
};

} // namespace

std::unique_ptr<IHttpClientUVE> CreatePlatformHttpClientUVE() {
    return std::make_unique<CurlHttpClientUVE>();
}

} // namespace UVE::Online

#endif
