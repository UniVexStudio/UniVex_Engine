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
inline constexpr std::chrono::seconds kDefaultHttpStallTimeout{30};
/// HTTP status classes (RFC 9110, section 15): [first, end) of the success and redirection ranges.
inline constexpr long kHttpSuccessFirst = 200;
inline constexpr long kHttpSuccessEnd = 300;
inline constexpr long kHttpRedirectionFirst = kHttpSuccessEnd;
inline constexpr long kHttpRedirectionEnd = 400;

[[nodiscard]] constexpr bool IsHttpSuccessStatus(long status) noexcept {
    return status >= kHttpSuccessFirst && status < kHttpSuccessEnd;
}
[[nodiscard]] constexpr bool IsHttpRedirectionStatus(long status) noexcept {
    return status >= kHttpRedirectionFirst && status < kHttpRedirectionEnd;
}

struct HttpHeader final {
    std::string name;
    std::string value;
};

struct HttpRequest final {
    std::string url;
    std::vector<HttpHeader> headers;
    /// Gives up when nothing arrives for this long.
    std::chrono::seconds stallTimeout = kDefaultHttpStallTimeout;
};

struct HttpResponse final {
    /// False when no HTTP response arrived at all (no network, DNS, TLS, timeout, cancelled).
    bool reached = false;
    long status = 0;
    std::string body;
    std::string error;

    [[nodiscard]] bool IsSuccess() const noexcept { return reached && IsHttpSuccessStatus(status); }
};

/// Called as a download progresses; `total` is 0 when the server did not say. Returning false
/// cancels the transfer.
using HttpProgress = std::function<bool(std::uint64_t received, std::uint64_t total)>;

/// HTTPS over the platform's own stack. Redirects are followed; an Authorization header is never
/// carried to a different host. Implementations are safe to use from one thread at a time.
class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    [[nodiscard]] virtual HttpResponse Get(const HttpRequest& request) = 0;
    /// Streams the response body into `file` (replacing it). The body of an error response is
    /// returned in the response instead of being written.
    [[nodiscard]] virtual HttpResponse Download(const HttpRequest& request, const std::filesystem::path& file,
                                                      const HttpProgress& progress) = 0;
};

/// The client for the platform this build targets.
[[nodiscard]] std::unique_ptr<IHttpClient> CreatePlatformHttpClient();

#if defined(__ANDROID__)
/// Android transfers go through the platform's Java HTTP stack; the app hands over its Java VM
/// (ANativeActivity::vm) once at startup, before the first request.
void SetAndroidJavaVm(void* javaVm);
#endif

} // namespace UVE::Online
