// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UVE::Online {

/// How long a transfer may go without receiving anything before it is abandoned.
inline constexpr std::chrono::seconds kDefaultHttpStallTimeoutUVE{30};
/// HTTP status classes (RFC 9110, section 15): [first, end) of the success and redirection ranges.
inline constexpr long kHttpSuccessFirstUVE = 200;
inline constexpr long kHttpSuccessEndUVE = 300;
inline constexpr long kHttpRedirectionFirstUVE = kHttpSuccessEndUVE;
inline constexpr long kHttpRedirectionEndUVE = 400;

[[nodiscard]] constexpr bool IsHttpSuccessStatusUVE(long status) noexcept {
    return status >= kHttpSuccessFirstUVE && status < kHttpSuccessEndUVE;
}
[[nodiscard]] constexpr bool IsHttpRedirectionStatusUVE(long status) noexcept {
    return status >= kHttpRedirectionFirstUVE && status < kHttpRedirectionEndUVE;
}

struct HttpHeaderUVE final {
    std::string name;
    std::string value;
};

struct HttpRequestUVE final {
    std::string url;
    std::vector<HttpHeaderUVE> headers;
    /// Gives up when nothing arrives for this long.
    std::chrono::seconds stallTimeout = kDefaultHttpStallTimeoutUVE;
};

struct HttpResponseUVE final {
    /// False when no HTTP response arrived at all (no network, DNS, TLS, timeout, cancelled).
    bool reached = false;
    long status = 0;
    std::string body;
    std::string error;

    [[nodiscard]] bool IsSuccessUVE() const noexcept { return reached && IsHttpSuccessStatusUVE(status); }
};

/// Called as a download progresses; `total` is 0 when the server did not say. Returning false
/// cancels the transfer.
using HttpProgressUVE = std::function<bool(std::uint64_t received, std::uint64_t total)>;

/// HTTPS over the platform's own stack. Redirects are followed; an Authorization header is never
/// carried to a different host. Implementations are safe to use from one thread at a time.
class IHttpClientUVE {
public:
    virtual ~IHttpClientUVE() = default;
    [[nodiscard]] virtual HttpResponseUVE GetUVE(const HttpRequestUVE& request) = 0;
    /// Streams the response body into `file` (replacing it). The body of an error response is
    /// returned in the response instead of being written.
    [[nodiscard]] virtual HttpResponseUVE DownloadUVE(const HttpRequestUVE& request, const std::filesystem::path& file,
                                                      const HttpProgressUVE& progress) = 0;
};

/// The client for the platform this build targets.
[[nodiscard]] std::unique_ptr<IHttpClientUVE> CreatePlatformHttpClientUVE();

#if defined(__ANDROID__)
/// Android transfers go through the platform's Java HTTP stack; the app hands over its Java VM
/// (ANativeActivity::vm) once at startup, before the first request.
void SetAndroidJavaVmUVE(void* javaVm);
#endif

} // namespace UVE::Online
