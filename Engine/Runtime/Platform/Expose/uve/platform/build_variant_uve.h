// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

/// Compile-time facts about the build variant this translation unit was compiled under.
///
/// The profile is chosen once, in Engine/CMake/UveBuildConfiguration.cmake, and arrives here as
/// preprocessor definitions. Everything downstream - assertions, logging, shader defines - reads
/// these rather than re-deriving intent from `NDEBUG`, because `NDEBUG` is a compiler-library
/// convention and not a statement about what kind of product is being built.
///
/// The static assertions below are the point of this header: a build whose definitions contradict
/// each other cannot compile. That matters most for the one combination nobody would notice by
/// reading a log - assertions enabled inside a shipping binary - which is silent, costs frame time,
/// and only shows up as a crash dialog a player sees.

#if !defined(UVE_BUILD_CONFIGURATION_DEBUG) && !defined(UVE_BUILD_CONFIGURATION_DEVELOPMENT) && \
    !defined(UVE_BUILD_CONFIGURATION_RELEASE) && !defined(UVE_BUILD_CONFIGURATION_SHIPPING)
#error "No UVE build profile is defined. Configure through Engine/CMake/UveBuildConfiguration.cmake."
#endif

#if !defined(UVE_PROFILE_ASSERTIONS_ENABLED) || !defined(UVE_PROFILE_DEFAULT_LOG_LEVEL) ||      \
    !defined(UVE_COMPILED_LOG_LEVEL) || !defined(UVE_SHIPPING)
#error "UVE build profile is incomplete: assertions, log levels and the shipping flag must all be defined."
#endif

namespace UVE::Build {

/// The profile this binary was built as. Values are stable so they can be logged, displayed in a
/// crash report, or written into a save header without a string table.
enum class ProfileUVE : unsigned char {
    Debug = 0,
    Development = 1,
    Release = 2,
    Shipping = 3,
};

#if defined(UVE_BUILD_CONFIGURATION_SHIPPING)
inline constexpr ProfileUVE kProfileUVE = ProfileUVE::Shipping;
#elif defined(UVE_BUILD_CONFIGURATION_RELEASE)
inline constexpr ProfileUVE kProfileUVE = ProfileUVE::Release;
#elif defined(UVE_BUILD_CONFIGURATION_DEVELOPMENT)
inline constexpr ProfileUVE kProfileUVE = ProfileUVE::Development;
#else
inline constexpr ProfileUVE kProfileUVE = ProfileUVE::Debug;
#endif

/// True when `UVE_ASSERT` evaluates its condition. A shipping binary must report false here; code
/// that would otherwise rely on an assertion firing has to handle the condition instead.
inline constexpr bool kAssertionsEnabledUVE = UVE_PROFILE_ASSERTIONS_ENABLED != 0;

/// True when this profile strips log statements at compile time instead of filtering them at
/// runtime. Only shipping does; every other profile keeps the statements and filters by level.
inline constexpr bool kCompiledOutLoggingUVE = UVE_COMPILED_LOG_LEVEL > 0;

/// Lowest severity this binary can emit, whether the filtering happened at compile time or will
/// happen at runtime. Exposed so diagnostics tooling can say "this build cannot print that" rather
/// than leaving a developer wondering why a message never appears.
inline constexpr int kCompiledLogLevelUVE = UVE_COMPILED_LOG_LEVEL;

[[nodiscard]] constexpr const char* ToStringUVE(const ProfileUVE profile) noexcept {
    switch (profile) {
        case ProfileUVE::Debug:
            return "Debug";
        case ProfileUVE::Development:
            return "Development";
        case ProfileUVE::Release:
            return "Release";
        case ProfileUVE::Shipping:
            return "Shipping";
    }
    return "Unknown";
}

/// Exactly one profile must be selected. Two would mean the CMake module published contradictory
/// definitions and every `#if` downstream would silently take whichever branch came first.
inline constexpr int kSelectedProfileCountUVE =
#if defined(UVE_BUILD_CONFIGURATION_DEBUG)
    1
#else
    0
#endif
#if defined(UVE_BUILD_CONFIGURATION_DEVELOPMENT)
    + 1
#else
    + 0
#endif
#if defined(UVE_BUILD_CONFIGURATION_RELEASE)
    + 1
#else
    + 0
#endif
#if defined(UVE_BUILD_CONFIGURATION_SHIPPING)
    + 1
#else
    + 0
#endif
    ;

static_assert(kSelectedProfileCountUVE == 1, "Exactly one UVE build profile must be defined.");

// The invariant worth a compile error: a shipping product must not carry assertion machinery.
static_assert(!(UVE_SHIPPING != 0) || UVE_PROFILE_ASSERTIONS_ENABLED == 0,
              "A shipping build must have UVE_PROFILE_ASSERTIONS_ENABLED=0; shipping with live "
              "assertions is a crash dialog in a player's face.");

// Compile-time log stripping below the runtime threshold would delete messages the profile still
// claims it can print. Above it, the profile would keep paying for statements it never emits.
static_assert(UVE_COMPILED_LOG_LEVEL == 0 || UVE_COMPILED_LOG_LEVEL == UVE_PROFILE_DEFAULT_LOG_LEVEL,
              "UVE_COMPILED_LOG_LEVEL must be either 0 (no stripping) or equal to the profile's "
              "runtime default; anything else makes the two disagree about what this binary emits.");

static_assert(UVE_PROFILE_DEFAULT_LOG_LEVEL >= 0 && UVE_PROFILE_DEFAULT_LOG_LEVEL <= 5,
              "UVE_PROFILE_DEFAULT_LOG_LEVEL must name a LogLevelUVE severity (0..5).");

} // namespace UVE::Build
