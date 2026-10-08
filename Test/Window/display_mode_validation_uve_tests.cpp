// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/window/display_mode_validation_uve.h"
#include "uve/window/window_mode_policy_uve.h"
#include <gtest/gtest.h>
namespace UVE::Window::Tests {
namespace {
TEST(DisplayModeValidationUVETest, AcceptsDefaultAndBoundedModes) {
    EXPECT_TRUE(ValidateDisplayModeDescUVE(DisplayModeDescUVE{}));
    EXPECT_TRUE(ValidateDisplayModeDescUVE({3840U, 2160U, 144U}));
    EXPECT_TRUE(ValidateDisplayModeDescUVE({kMaximumDisplayModeAxisUVE, kMaximumDisplayModeAxisUVE,
                                            kMaximumDisplayModeRefreshRateUVE}));
}
TEST(DisplayModeValidationUVETest, AcceptsZeroRefreshAsBackendAutoMode) {
    EXPECT_TRUE(ValidateDisplayModeDescUVE({1280U, 720U, 0U}));
}
TEST(DisplayModeValidationUVETest, RejectsZeroOrOversizedDimensions) {
    EXPECT_FALSE(ValidateDisplayModeDescUVE({0U, 1080U, 60U}));
    EXPECT_FALSE(ValidateDisplayModeDescUVE({1920U, 0U, 60U}));
    EXPECT_FALSE(ValidateDisplayModeDescUVE({kMaximumDisplayModeAxisUVE + 1U, 1080U, 60U}));
    EXPECT_FALSE(ValidateDisplayModeDescUVE({1920U, kMaximumDisplayModeAxisUVE + 1U, 60U}));
}
TEST(DisplayModeValidationUVETest, RejectsExcessiveRefreshRate) {
    EXPECT_FALSE(ValidateDisplayModeDescUVE({1920U, 1080U, kMaximumDisplayModeRefreshRateUVE + 1U}));
}

TEST(WindowModePolicyUVETest, FallsBackToWindowedOnlyWhenFullscreenLacksMonitorMode) {
    using Platform::WindowModeUVE;
    for (const WindowModeUVE mode : {WindowModeUVE::Windowed, WindowModeUVE::Maximized,
                                     WindowModeUVE::Borderless, WindowModeUVE::Fullscreen,
                                     WindowModeUVE::ExclusiveFullscreen}) {
        EXPECT_EQ(ResolveInitialWindowModeUVE(mode, true), mode);
    }

    EXPECT_EQ(ResolveInitialWindowModeUVE(WindowModeUVE::Windowed, false), WindowModeUVE::Windowed);
    EXPECT_EQ(ResolveInitialWindowModeUVE(WindowModeUVE::Maximized, false), WindowModeUVE::Maximized);
    EXPECT_EQ(ResolveInitialWindowModeUVE(WindowModeUVE::Borderless, false), WindowModeUVE::Borderless);
    EXPECT_EQ(ResolveInitialWindowModeUVE(WindowModeUVE::Fullscreen, false), WindowModeUVE::Windowed);
    EXPECT_EQ(ResolveInitialWindowModeUVE(WindowModeUVE::ExclusiveFullscreen, false), WindowModeUVE::Windowed);
}

TEST(WindowModePolicyUVETest, ClassifiesOnlyMonitorFullscreenModesAsFullscreen) {
    using Platform::WindowModeUVE;
    EXPECT_TRUE(IsFullscreenWindowModeUVE(WindowModeUVE::Fullscreen));
    EXPECT_TRUE(IsFullscreenWindowModeUVE(WindowModeUVE::ExclusiveFullscreen));
    EXPECT_FALSE(IsFullscreenWindowModeUVE(WindowModeUVE::Windowed));
    EXPECT_FALSE(IsFullscreenWindowModeUVE(WindowModeUVE::Maximized));
    EXPECT_FALSE(IsFullscreenWindowModeUVE(WindowModeUVE::Borderless));
}

TEST(WindowModePolicyUVETest, PreservesRestoreBoundsWhileMaximizedOrFullscreen) {
    EXPECT_TRUE(ShouldCaptureWindowedGeometryUVE(false, false));
    EXPECT_FALSE(ShouldCaptureWindowedGeometryUVE(false, true));
    EXPECT_FALSE(ShouldCaptureWindowedGeometryUVE(true, false));
    EXPECT_FALSE(ShouldCaptureWindowedGeometryUVE(true, true));
}
} // namespace
} // namespace UVE::Window::Tests
