// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string_view>

namespace UVE::Debug {

/// Severity level of a log message, from least to most severe. Values are
/// ordered so `level >= threshold` comparisons work as expected.
enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5,
};

/// Returns the fixed-width display name for a log level (e.g. "WARNING").
/// Returns "UNKNOWN" for a value outside the declared enumerators (only
/// reachable via an invalid static_cast from an integer, never from normal
/// engine use).
[[nodiscard]] constexpr std::string_view ToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARNING";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
    }
    return "UNKNOWN";
}

} // namespace UVE::Debug
