// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vsync_policy.h"

#include <gtest/gtest.h>

namespace UVE::Window::Tests {
namespace {

TEST(VSyncPolicyTest, OffAndOnMapToStandardSwapIntervals) {
    const OpenGlVSyncResolution off = ResolveOpenGlVSync(Platform::VSyncMode::Off, false);
    EXPECT_EQ(off.effectiveMode, Platform::VSyncMode::Off);
    EXPECT_EQ(off.swapInterval, 0);

    const OpenGlVSyncResolution on = ResolveOpenGlVSync(Platform::VSyncMode::On, false);
    EXPECT_EQ(on.effectiveMode, Platform::VSyncMode::On);
    EXPECT_EQ(on.swapInterval, 1);
}

TEST(VSyncPolicyTest, AdaptiveUsesNegativeIntervalWhenSupportedAndFallsBackToOnOtherwise) {
    const OpenGlVSyncResolution supported =
        ResolveOpenGlVSync(Platform::VSyncMode::Adaptive, true);
    EXPECT_EQ(supported.effectiveMode, Platform::VSyncMode::Adaptive);
    EXPECT_EQ(supported.swapInterval, -1);

    const OpenGlVSyncResolution unsupported =
        ResolveOpenGlVSync(Platform::VSyncMode::Adaptive, false);
    EXPECT_EQ(unsupported.effectiveMode, Platform::VSyncMode::On);
    EXPECT_EQ(unsupported.swapInterval, 1);
}

TEST(VSyncPolicyTest, MailboxFallsBackToNonBlockingOffOnOpenGl) {
    const OpenGlVSyncResolution mailbox =
        ResolveOpenGlVSync(Platform::VSyncMode::Mailbox, true);
    EXPECT_EQ(mailbox.effectiveMode, Platform::VSyncMode::Off);
    EXPECT_EQ(mailbox.swapInterval, 0);
}

} // namespace
} // namespace UVE::Window::Tests
