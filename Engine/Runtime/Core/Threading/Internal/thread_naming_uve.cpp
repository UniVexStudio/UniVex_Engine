// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// _GNU_SOURCE arrives via target_compile_definitions in this module's CMakeLists (Linux only):
// glibc exposes pthread_setname_np/getname_np only with it.

#include "uve/threading/thread_naming_uve.h"

#include <array>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace UVE::Threading {

#if defined(_WIN32)

bool SetCurrentThreadNameUVE(const std::string_view name) noexcept {
    // SetThreadDescription takes UTF-16; thread names here are ASCII by convention, so a
    // widening copy (no MultiByteToWideChar machinery) is exact for every name we set.
    std::array<wchar_t, 64U> wide{};
    std::size_t length = 0U;
    for (const char c : name) {
        if (length + 1U >= wide.size()) {
            break;
        }
        wide[length] = static_cast<wchar_t>(static_cast<unsigned char>(c));
        ++length;
    }
    wide[length] = L'\0';
    return SUCCEEDED(SetThreadDescription(GetCurrentThread(), wide.data()));
}

bool TryGetCurrentThreadNameUVE(std::string& outName) {
    PWSTR description = nullptr;
    if (FAILED(GetThreadDescription(GetCurrentThread(), &description))) {
        return false;
    }
    std::string fetched;
    for (const wchar_t* c = description; *c != L'\0'; ++c) {
        fetched.push_back(static_cast<char>(*c));
    }
    LocalFree(description);
    outName = std::move(fetched);
    return true;
}

#elif defined(__linux__)

namespace {

/// Linux thread names hold 15 bytes plus the terminator; pthread_setname_np fails past it.
constexpr std::size_t kMaxThreadNameBytesUVE = 15U;

} // namespace

bool SetCurrentThreadNameUVE(const std::string_view name) noexcept {
    std::array<char, kMaxThreadNameBytesUVE + 1U> truncated{};
    const std::size_t length =
        name.size() < kMaxThreadNameBytesUVE ? name.size() : kMaxThreadNameBytesUVE;
    for (std::size_t i = 0U; i < length; ++i) {
        truncated[i] = name[i];
    }
    truncated[length] = '\0';
    return pthread_setname_np(pthread_self(), truncated.data()) == 0;
}

bool TryGetCurrentThreadNameUVE(std::string& outName) {
    std::array<char, kMaxThreadNameBytesUVE + 1U> fetched{};
    if (pthread_getname_np(pthread_self(), fetched.data(), fetched.size()) != 0) {
        return false;
    }
    outName.assign(fetched.data());
    return true;
}

#elif defined(__APPLE__)

bool SetCurrentThreadNameUVE(const std::string_view name) noexcept {
    // macOS names only the calling thread (no handle overload exists) and caps at 63 bytes.
    std::array<char, 64U> truncated{};
    const std::size_t length = name.size() < 63U ? name.size() : 63U;
    for (std::size_t i = 0U; i < length; ++i) {
        truncated[i] = name[i];
    }
    truncated[length] = '\0';
    return pthread_setname_np(truncated.data()) == 0;
}

bool TryGetCurrentThreadNameUVE(std::string& outName) {
    std::array<char, 64U> fetched{};
    if (pthread_getname_np(pthread_self(), fetched.data(), fetched.size()) != 0) {
        return false;
    }
    outName.assign(fetched.data());
    return true;
}

#else

bool SetCurrentThreadNameUVE(const std::string_view /*name*/) noexcept {
    return false;
}

bool TryGetCurrentThreadNameUVE(std::string& /*outName*/) {
    return false;
}

#endif

} // namespace UVE::Threading
