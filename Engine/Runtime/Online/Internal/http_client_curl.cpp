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
inline constexpr long kMaximumRedirects = 5L;
/// Below this many bytes per second for the stall timeout, a transfer counts as stalled.
inline constexpr long kStallBytesPerSecond = 1L;
inline constexpr long kConnectTimeoutSeconds = 15L;

void EnsureGlobalInit() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct DownloadSink final {
    std::ofstream* file = nullptr;
    std::string* errorBody = nullptr;
    CURL* handle = nullptr;
    bool failed = false;
};

std::size_t AppendToString(char* data, const std::size_t size, const std::size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

std::size_t WriteDownload(char* data, const std::size_t size, const std::size_t count, void* user) {
    auto* sink = static_cast<DownloadSink*>(user);
    long status = 0;
    curl_easy_getinfo(sink->handle, CURLINFO_RESPONSE_CODE, &status);
    // An error page is kept for the message rather than written as the file.
    if (!IsHttpSuccessStatus(status)) {
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

int ReportProgress(void* user, const curl_off_t total, const curl_off_t received, curl_off_t, curl_off_t) {
    const auto* progress = static_cast<const HttpProgress*>(user);
    if (progress != nullptr && *progress &&
        !(*progress)(static_cast<std::uint64_t>(received), static_cast<std::uint64_t>(total))) {
        return 1; // non-zero aborts the transfer
    }
    return 0;
}

/// One configured easy handle with its header list, released together.
class CurlRequest final {
public:
    explicit CurlRequest(const HttpRequest& request) : m_handle(curl_easy_init()) {
        if (m_handle == nullptr) {
            return;
        }
        for (const HttpHeader& header : request.headers) {
            m_headers = curl_slist_append(m_headers, (header.name + ": " + header.value).c_str());
        }
        curl_easy_setopt(m_handle, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, m_headers);
        curl_easy_setopt(m_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(m_handle, CURLOPT_MAXREDIRS, kMaximumRedirects);
        // Never hand credentials to the host a redirect points at.
        curl_easy_setopt(m_handle, CURLOPT_UNRESTRICTED_AUTH, 0L);
        curl_easy_setopt(m_handle, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(m_handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        curl_easy_setopt(m_handle, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
        curl_easy_setopt(m_handle, CURLOPT_LOW_SPEED_LIMIT, kStallBytesPerSecond);
        curl_easy_setopt(m_handle, CURLOPT_LOW_SPEED_TIME, static_cast<long>(request.stallTimeout.count()));
        curl_easy_setopt(m_handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(m_handle, CURLOPT_ERRORBUFFER, m_error);
    }
    CurlRequest(const CurlRequest&) = delete;
    CurlRequest& operator=(const CurlRequest&) = delete;
    ~CurlRequest() {
        curl_slist_free_all(m_headers);
        if (m_handle != nullptr) {
            curl_easy_cleanup(m_handle);
        }
    }

    [[nodiscard]] CURL* Handle() const noexcept { return m_handle; }

    [[nodiscard]] HttpResponse Perform() {
        HttpResponse response;
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

class CurlHttpClient final : public IHttpClient {
public:
    CurlHttpClient() { EnsureGlobalInit(); }

    HttpResponse Get(const HttpRequest& request) override {
        CurlRequest curl(request);
        if (curl.Handle() == nullptr) {
            return {false, 0, {}, "Unable to start an HTTPS request."};
        }
        std::string body;
        curl_easy_setopt(curl.Handle(), CURLOPT_WRITEFUNCTION, &AppendToString);
        curl_easy_setopt(curl.Handle(), CURLOPT_WRITEDATA, &body);
        HttpResponse response = curl.Perform();
        response.body = std::move(body);
        return response;
    }

    HttpResponse Download(const HttpRequest& request, const std::filesystem::path& file,
                                const HttpProgress& progress) override {
        CurlRequest curl(request);
        if (curl.Handle() == nullptr) {
            return {false, 0, {}, "Unable to start an HTTPS request."};
        }
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return {false, 0, {}, "Unable to write " + file.string()};
        }
        std::string errorBody;
        DownloadSink sink{&output, &errorBody, curl.Handle(), false};
        curl_easy_setopt(curl.Handle(), CURLOPT_WRITEFUNCTION, &WriteDownload);
        curl_easy_setopt(curl.Handle(), CURLOPT_WRITEDATA, &sink);
        curl_easy_setopt(curl.Handle(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.Handle(), CURLOPT_XFERINFOFUNCTION, &ReportProgress);
        curl_easy_setopt(curl.Handle(), CURLOPT_XFERINFODATA, &progress);
        HttpResponse response = curl.Perform();
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

std::unique_ptr<IHttpClient> CreatePlatformHttpClient() {
    return std::make_unique<CurlHttpClient>();
}

} // namespace UVE::Online

#endif
