// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string_view>
#include <utility>

#include "uve/logging/log_format_uve.h"
#include "uve/logging/log_level_uve.h"
#include "uve/logging/logger_uve.h"
#include "uve/platform/platform_uve.h"

namespace UVE::Debug::Detail {

/// Formats `formatString`/`args` and routes the result to the currently
/// active LoggerUVE (see LoggerUVE::GetActiveInstanceUVE()), tagged with the
/// given severity level and source location. Safely does nothing if no
/// logger is currently active (before engine Init() or after Shutdown()).
/// Not intended to be called directly — use the UVE_LOG-family macros below.
/// Thread-safety: safe to call from any thread; delegates entirely to
/// LoggerUVE, which is itself thread-safe.
template <typename... TArgs>
UVE_INLINE void LogToActiveInstanceUVE(
    LogLevelUVE level,
    const char* sourceFile,
    int sourceLine,
#if UVE_HAS_STD_FORMAT
    std::format_string<TArgs...> formatString,
#else
    fmt::format_string<TArgs...> formatString,
#endif
    TArgs&&... args) {
    LoggerUVE* const activeLogger = LoggerUVE::GetActiveInstanceUVE();
    if (activeLogger == nullptr) {
        return;
    }
    activeLogger->LogFormatted(level, std::string_view{}, sourceFile, sourceLine,
                                FormatLogMessageUVE(formatString, std::forward<TArgs>(args)...));
}

} // namespace UVE::Debug::Detail

/// Logs a formatted message at an explicit LogLevelUVE. `fmt` is a
/// std::format/{fmt}-style format string (compile-time validated against
/// `...`); prefer the UVE_TRACE/INFO/WARNING/ERROR/FATAL convenience macros
/// below unless the level is only known at runtime as a variable.
#define UVE_LOG(level, fmt, ...) \
    ::UVE::Debug::Detail::LogToActiveInstanceUVE((level), __FILE__, __LINE__, fmt __VA_OPT__(, ) __VA_ARGS__)

/// Compile-time log stripping.
///
/// In a shipping profile the statements below its runtime threshold are removed by the
/// PREPROCESSOR, not filtered by the logger. The difference is not cosmetic: a runtime filter still
/// puts the format string, its literal fragments and the call in the binary, so a shipping build
/// would carry every INFO message it is guaranteed never to print. Stripping here means the
/// compiler never sees those calls at all.
///
/// The threshold is `UVE_COMPILED_LOG_LEVEL`, which the build profile sets from its own runtime
/// default - so nothing that would have been printed disappears, and a build variant cannot
/// disagree with itself about what it emits (build_variant_uve.h turns that into a compile error).
/// Every profile except shipping sets 0, which compiles the macros back to the plain logging
/// call and leaves behaviour exactly as it was.
///
/// A stripped call does not evaluate its arguments. That is the point, but it also means a value
/// computed ONLY for a log line becomes unused in shipping; mark such values `[[maybe_unused]]`
/// rather than reintroducing the call.
#ifndef UVE_COMPILED_LOG_LEVEL
#define UVE_COMPILED_LOG_LEVEL 0
#endif

/// Logs a formatted message at LogLevelUVE::Trace.
#if UVE_COMPILED_LOG_LEVEL > 0
#define UVE_TRACE(fmt, ...) ((void)0)
#else
#define UVE_TRACE(fmt, ...) UVE_LOG(::UVE::Debug::LogLevelUVE::Trace, fmt __VA_OPT__(, ) __VA_ARGS__)
#endif
/// Logs a formatted message at LogLevelUVE::Info.
#if UVE_COMPILED_LOG_LEVEL > 2
#define UVE_INFO(fmt, ...) ((void)0)
#else
#define UVE_INFO(fmt, ...) UVE_LOG(::UVE::Debug::LogLevelUVE::Info, fmt __VA_OPT__(, ) __VA_ARGS__)
#endif
/// Logs a formatted message at LogLevelUVE::Warning.
#if UVE_COMPILED_LOG_LEVEL > 3
#define UVE_WARNING(fmt, ...) ((void)0)
#else
#define UVE_WARNING(fmt, ...) UVE_LOG(::UVE::Debug::LogLevelUVE::Warning, fmt __VA_OPT__(, ) __VA_ARGS__)
#endif
/// Logs a formatted message at LogLevelUVE::Error.
#if UVE_COMPILED_LOG_LEVEL > 4
#define UVE_ERROR(fmt, ...) ((void)0)
#else
#define UVE_ERROR(fmt, ...) UVE_LOG(::UVE::Debug::LogLevelUVE::Error, fmt __VA_OPT__(, ) __VA_ARGS__)
#endif
/// Logs a formatted message at LogLevelUVE::Fatal. Fatal messages are
/// guaranteed to be flushed to every sink before LogFormatted() returns.
#define UVE_FATAL(fmt, ...) UVE_LOG(::UVE::Debug::LogLevelUVE::Fatal, fmt __VA_OPT__(, ) __VA_ARGS__)
