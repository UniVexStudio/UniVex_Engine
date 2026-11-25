// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "build_variant.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "log_level.h"
#include "logging_macros.h"
#include "platform.h"

namespace UVE::Build::Tests {
namespace {

/// The profile this test binary was configured as. These tests only ever run under a profile, so
/// they assert that the compile-time facts agree with the definitions the CMake module published -
/// which is what catches a module that publishes one thing and documents another.
constexpr Profile kExpectedProfile =
#if defined(UVE_BUILD_CONFIGURATION_SHIPPING)
    Profile::Shipping;
#elif defined(UVE_BUILD_CONFIGURATION_RELEASE)
    Profile::Release;
#elif defined(UVE_BUILD_CONFIGURATION_DEVELOPMENT)
    Profile::Development;
#else
    Profile::Debug;
#endif

} // namespace

TEST(BuildVariantTest, ExactlyOneProfileIsSelectedAndItIsTheExpectedOne) {
    EXPECT_EQ(kSelectedProfileCount, 1);
    EXPECT_EQ(kProfile, kExpectedProfile);
}

TEST(BuildVariantTest, CompileTimeFactsMatchThePublishedDefinitions) {
    // The header's constants are what downstream code branches on; the macros are what CMake
    // actually published. If these ever disagree, a reader would be trusting a constant that does
    // not describe the binary.
    EXPECT_EQ(kAssertionsEnabled, UVE_PROFILE_ASSERTIONS_ENABLED != 0);
    EXPECT_EQ(kCompiledOutLogging, UVE_COMPILED_LOG_LEVEL > 0);
    EXPECT_EQ(kCompiledLogLevel, UVE_COMPILED_LOG_LEVEL);
    EXPECT_EQ(UVE_DEBUG, UVE_PROFILE_ASSERTIONS_ENABLED);
}

TEST(BuildVariantTest, ShippingNeverCarriesLiveAssertions) {
    // The one combination that is silent, expensive and player-visible: shipping with assertion
    // machinery still in the binary. It is also a compile error (build_variant_uve.h), so this test
    // exists to keep the property stated where a reader will look for behaviour rather than rules.
    if (kProfile == Profile::Shipping) {
        EXPECT_FALSE(kAssertionsEnabled);
    }
    EXPECT_FALSE(UVE_SHIPPING != 0 && UVE_PROFILE_ASSERTIONS_ENABLED != 0);
}

TEST(BuildVariantTest, CompileTimeLogLevelAgreesWithTheRuntimeDefault) {
    // 0 means "no stripping"; anything else must equal the profile's own runtime threshold, or the
    // build would claim it can print messages it has already deleted.
    EXPECT_TRUE(UVE_COMPILED_LOG_LEVEL == 0 ||
                UVE_COMPILED_LOG_LEVEL == UVE_PROFILE_DEFAULT_LOG_LEVEL);
    if (kCompiledOutLogging) {
        EXPECT_EQ(kCompiledLogLevel, UVE_PROFILE_DEFAULT_LOG_LEVEL);
    }
}

TEST(BuildVariantTest, EveryProfileHasADistinctStableName) {
    const std::array<Profile, 4> profiles{Profile::Debug, Profile::Development,
                                             Profile::Release, Profile::Shipping};
    for (std::size_t first = 0; first < profiles.size(); ++first) {
        EXPECT_STRNE(ToString(profiles[first]), "Unknown");
        for (std::size_t second = first + 1U; second < profiles.size(); ++second) {
            // Distinct because a name is what lands in a crash report or a save header; two profiles
            // sharing one would make those artifacts unreadable after the fact.
            EXPECT_STRNE(ToString(profiles[first]), ToString(profiles[second]));
        }
    }
}

TEST(BuildVariantTest, RuntimeLogLevelIsWithinTheDeclaredSeverityRange) {
    EXPECT_GE(UVE_PROFILE_DEFAULT_LOG_LEVEL, 0);
    EXPECT_LE(UVE_PROFILE_DEFAULT_LOG_LEVEL, static_cast<int>(Debug::LogLevel::Fatal));
}

TEST(BuildVariantTest, LogMacrosStillEvaluateTheirArgumentsWhenNotStripped) {
    // Locks the non-shipping half of the contract: a log call that silently stopped evaluating its
    // arguments would turn every `UVE_INFO("{}", Compute())` into dead code without a diagnostic.
    // Under a stripping profile the opposite is true by design, so the assertion is skipped there.
    int sideEffects = 0;
    const auto bump = [&sideEffects]() {
        ++sideEffects;
        return 1;
    };
#if UVE_COMPILED_LOG_LEVEL > 2
    UVE_INFO("side effect {}", bump());
    EXPECT_EQ(sideEffects, 0);
#else
    UVE_INFO("side effect {}", bump());
    EXPECT_EQ(sideEffects, 1);
#endif
}

} // namespace UVE::Build::Tests
