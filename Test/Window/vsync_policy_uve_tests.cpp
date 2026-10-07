// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/window/vsync_policy_uve.h"

#include <gtest/gtest.h>

namespace UVE::Window::Tests {
namespace {

TEST(VSyncPolicyUVETest, OffAndOnMapToStandardSwapIntervals) {
    const OpenGlVSyncResolutionUVE off = ResolveOpenGlVSyncUVE(Platform::VSyncModeUVE::Off, false);
    EXPECT_EQ(off.effectiveMode, Platform::VSyncModeUVE::Off);
    EXPECT_EQ(off.swapInterval, 0);

    const OpenGlVSyncResolutionUVE on = ResolveOpenGlVSyncUVE(Platform::VSyncModeUVE::On, false);
    EXPECT_EQ(on.effectiveMode, Platform::VSyncModeUVE::On);
    EXPECT_EQ(on.swapInterval, 1);
}

TEST(VSyncPolicyUVETest, AdaptiveUsesNegativeIntervalWhenSupportedAndFallsBackToOnOtherwise) {
    const OpenGlVSyncResolutionUVE supported =
        ResolveOpenGlVSyncUVE(Platform::VSyncModeUVE::Adaptive, true);
    EXPECT_EQ(supported.effectiveMode, Platform::VSyncModeUVE::Adaptive);
    EXPECT_EQ(supported.swapInterval, -1);

    const OpenGlVSyncResolutionUVE unsupported =
        ResolveOpenGlVSyncUVE(Platform::VSyncModeUVE::Adaptive, false);
    EXPECT_EQ(unsupported.effectiveMode, Platform::VSyncModeUVE::On);
    EXPECT_EQ(unsupported.swapInterval, 1);
}

TEST(VSyncPolicyUVETest, MailboxFallsBackToNonBlockingOffOnOpenGl) {
    const OpenGlVSyncResolutionUVE mailbox =
        ResolveOpenGlVSyncUVE(Platform::VSyncModeUVE::Mailbox, true);
    EXPECT_EQ(mailbox.effectiveMode, Platform::VSyncModeUVE::Off);
    EXPECT_EQ(mailbox.swapInterval, 0);
}

} // namespace
} // namespace UVE::Window::Tests
