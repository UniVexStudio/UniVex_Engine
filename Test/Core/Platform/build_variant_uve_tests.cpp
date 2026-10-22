// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/platform/build_variant_uve.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "uve/logging/log_level_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/platform/platform_uve.h"

namespace UVE::Build::Tests {
namespace {

/// The profile this test binary was configured as. These tests only ever run under a profile, so
/// they assert that the compile-time facts agree with the definitions the CMake module published -
/// which is what catches a module that publishes one thing and documents another.
constexpr ProfileUVE kExpectedProfileUVE =
#if defined(UVE_BUILD_CONFIGURATION_SHIPPING)
    ProfileUVE::Shipping;
#elif defined(UVE_BUILD_CONFIGURATION_RELEASE)
    ProfileUVE::Release;
#elif defined(UVE_BUILD_CONFIGURATION_DEVELOPMENT)
    ProfileUVE::Development;
#else
    ProfileUVE::Debug;
#endif

} // namespace

TEST(BuildVariantUVETest, ExactlyOneProfileIsSelectedAndItIsTheExpectedOne) {
    EXPECT_EQ(kSelectedProfileCountUVE, 1);
    EXPECT_EQ(kProfileUVE, kExpectedProfileUVE);
}

TEST(BuildVariantUVETest, CompileTimeFactsMatchThePublishedDefinitions) {
    // The header's constants are what downstream code branches on; the macros are what CMake
    // actually published. If these ever disagree, a reader would be trusting a constant that does
    // not describe the binary.
    EXPECT_EQ(kAssertionsEnabledUVE, UVE_PROFILE_ASSERTIONS_ENABLED != 0);
    EXPECT_EQ(kCompiledOutLoggingUVE, UVE_COMPILED_LOG_LEVEL > 0);
    EXPECT_EQ(kCompiledLogLevelUVE, UVE_COMPILED_LOG_LEVEL);
    EXPECT_EQ(UVE_DEBUG, UVE_PROFILE_ASSERTIONS_ENABLED);
}

TEST(BuildVariantUVETest, ShippingNeverCarriesLiveAssertions) {
    // The one combination that is silent, expensive and player-visible: shipping with assertion
    // machinery still in the binary. It is also a compile error (build_variant_uve.h), so this test
    // exists to keep the property stated where a reader will look for behaviour rather than rules.
    if (kProfileUVE == ProfileUVE::Shipping) {
        EXPECT_FALSE(kAssertionsEnabledUVE);
    }
    EXPECT_FALSE(UVE_SHIPPING != 0 && UVE_PROFILE_ASSERTIONS_ENABLED != 0);
}

TEST(BuildVariantUVETest, CompileTimeLogLevelAgreesWithTheRuntimeDefault) {
    // 0 means "no stripping"; anything else must equal the profile's own runtime threshold, or the
    // build would claim it can print messages it has already deleted.
    EXPECT_TRUE(UVE_COMPILED_LOG_LEVEL == 0 ||
                UVE_COMPILED_LOG_LEVEL == UVE_PROFILE_DEFAULT_LOG_LEVEL);
    if (kCompiledOutLoggingUVE) {
        EXPECT_EQ(kCompiledLogLevelUVE, UVE_PROFILE_DEFAULT_LOG_LEVEL);
    }
}

TEST(BuildVariantUVETest, EveryProfileHasADistinctStableName) {
    const std::array<ProfileUVE, 4> profiles{ProfileUVE::Debug, ProfileUVE::Development,
                                             ProfileUVE::Release, ProfileUVE::Shipping};
    for (std::size_t first = 0; first < profiles.size(); ++first) {
        EXPECT_STRNE(ToStringUVE(profiles[first]), "Unknown");
        for (std::size_t second = first + 1U; second < profiles.size(); ++second) {
            // Distinct because a name is what lands in a crash report or a save header; two profiles
            // sharing one would make those artifacts unreadable after the fact.
            EXPECT_STRNE(ToStringUVE(profiles[first]), ToStringUVE(profiles[second]));
        }
    }
}

TEST(BuildVariantUVETest, RuntimeLogLevelIsWithinTheDeclaredSeverityRange) {
    EXPECT_GE(UVE_PROFILE_DEFAULT_LOG_LEVEL, 0);
    EXPECT_LE(UVE_PROFILE_DEFAULT_LOG_LEVEL, static_cast<int>(Debug::LogLevelUVE::Fatal));
}

TEST(BuildVariantUVETest, LogMacrosStillEvaluateTheirArgumentsWhenNotStripped) {
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
