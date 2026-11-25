// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "http_client.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <array>
#include <fstream>
#include <string>
#include <system_error>

namespace UVE::Online {
namespace {

inline constexpr DWORD kMaximumRedirects = 5U;
inline constexpr std::size_t kReadChunkBytes = 1U << 16U;
inline constexpr wchar_t kUserAgent[] = L"UniVex";
inline constexpr std::wstring_view kAuthorizationHeader = L"Authorization";

[[nodiscard]] std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

[[nodiscard]] std::string LastError(const char* what) {
    return std::string(what) + ": " + std::system_category().message(static_cast<int>(GetLastError()));
}

/// Owns a WinHTTP handle.
class WinHttpHandle final {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) : m_handle(handle) {}
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    ~WinHttpHandle() {
        if (m_handle != nullptr) {
            WinHttpCloseHandle(m_handle);
        }
    }
    [[nodiscard]] HINTERNET Get() const noexcept { return m_handle; }

private:
    HINTERNET m_handle;
};

struct ParsedUrl final {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
};

[[nodiscard]] bool ParseHttpsUrl(const std::wstring& url, ParsedUrl& parsed) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components) || components.nScheme != INTERNET_SCHEME_HTTPS) {
        return false;
    }
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    parsed.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    parsed.port = components.nPort;
    return true;
}

/// Performs one request. Redirects are followed by hand so the Authorization header is dropped
/// whenever the host changes. `sink` receives successful body bytes; error bodies go to the response.
template <class Sink>
[[nodiscard]] HttpResponse Perform(const HttpRequest& request, Sink&& sink, const HttpProgress* progress) {
    HttpResponse response;
    WinHttpHandle session(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.Get() == nullptr) {
        response.error = LastError("Unable to start WinHTTP");
        return response;
    }
    const int timeoutMs =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(request.stallTimeout).count());
    WinHttpSetTimeouts(session.Get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    std::wstring url = Widen(request.url);
    std::wstring originalHost;
    for (DWORD redirect = 0U; redirect <= kMaximumRedirects; ++redirect) {
        ParsedUrl parsed;
        if (!ParseHttpsUrl(url, parsed)) {
            response.error = "Only https addresses are allowed.";
            return response;
        }
        if (originalHost.empty()) {
            originalHost = parsed.host;
        }
        WinHttpHandle connection(WinHttpConnect(session.Get(), parsed.host.c_str(), parsed.port, 0));
        WinHttpHandle handle(connection.Get() == nullptr
                                    ? nullptr
                                    : WinHttpOpenRequest(connection.Get(), L"GET", parsed.path.c_str(), nullptr,
                                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                         WINHTTP_FLAG_SECURE));
        if (handle.Get() == nullptr) {
            response.error = LastError("Unable to open the request");
            return response;
        }
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(handle.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        std::wstring headers;
        for (const HttpHeader& header : request.headers) {
            const std::wstring name = Widen(header.name);
            if (parsed.host != originalHost && _wcsicmp(name.c_str(), kAuthorizationHeader.data()) == 0) {
                continue;
            }
            headers += name + L": " + Widen(header.value) + L"\r\n";
        }
        if (!WinHttpSendRequest(handle.Get(), headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                headers.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(handle.Get(), nullptr)) {
            response.error = LastError("The request failed");
            return response;
        }
        DWORD status = 0U;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(handle.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        if (IsHttpRedirectionStatus(static_cast<long>(status))) {
            DWORD locationBytes = 0U;
            WinHttpQueryHeaders(handle.Get(), WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                                &locationBytes, WINHTTP_NO_HEADER_INDEX);
            std::wstring location(locationBytes / sizeof(wchar_t), L'\0');
            if (locationBytes == 0U ||
                !WinHttpQueryHeaders(handle.Get(), WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                     location.data(), &locationBytes, WINHTTP_NO_HEADER_INDEX)) {
                response.error = "A redirect had no location.";
                return response;
            }
            location.resize(locationBytes / sizeof(wchar_t));
            url = location;
            continue;
        }
        response.reached = true;
        response.status = static_cast<long>(status);
        DWORD lengthValue = 0U;
        DWORD lengthSize = sizeof(lengthValue);
        const std::uint64_t total =
            WinHttpQueryHeaders(handle.Get(), WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &lengthValue, &lengthSize, WINHTTP_NO_HEADER_INDEX)
                ? lengthValue
                : 0U;
        std::array<char, kReadChunkBytes> chunk{};
        std::uint64_t received = 0U;
        while (true) {
            DWORD read = 0U;
            if (!WinHttpReadData(handle.Get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read)) {
                response.reached = false;
                response.error = LastError("The transfer failed");
                return response;
            }
            if (read == 0U) {
                break;
            }
            if (response.IsSuccess()) {
                if (!sink(chunk.data(), read)) {
                    response.reached = false;
                    response.error = "Unable to store the downloaded data.";
                    return response;
                }
            } else {
                response.body.append(chunk.data(), read);
            }
            received += read;
            if (progress != nullptr && *progress && !(*progress)(received, total)) {
                response.reached = false;
                response.error = "Cancelled.";
                return response;
            }
        }
        return response;
    }
    response.error = "Too many redirects.";
    return response;
}

class WinHttpClient final : public IHttpClient {
public:
    HttpResponse Get(const HttpRequest& request) override {
        std::string body;
        HttpResponse response = Perform(request,
                                              [&body](const char* data, const DWORD size) {
                                                  body.append(data, size);
                                                  return true;
                                              },
                                              nullptr);
        if (response.IsSuccess()) {
            response.body = std::move(body);
        }
        return response;
    }

    HttpResponse Download(const HttpRequest& request, const std::filesystem::path& file,
                                const HttpProgress& progress) override {
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return {false, 0, {}, "Unable to write " + file.string()};
        }
        return Perform(request,
                          [&output](const char* data, const DWORD size) {
                              output.write(data, static_cast<std::streamsize>(size));
                              return static_cast<bool>(output);
                          },
                          &progress);
    }
};

} // namespace

std::unique_ptr<IHttpClient> CreatePlatformHttpClient() {
    return std::make_unique<WinHttpClient>();
}

} // namespace UVE::Online

#endif
