// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "display_mode_validation.h"
#include "window_mode_policy.h"
#include <gtest/gtest.h>
namespace UVE::Window::Tests {
namespace {
TEST(DisplayModeValidationTest, AcceptsDefaultAndBoundedModes) {
    EXPECT_TRUE(ValidateDisplayModeDesc(DisplayModeDesc{}));
    EXPECT_TRUE(ValidateDisplayModeDesc({3840U, 2160U, 144U}));
    EXPECT_TRUE(ValidateDisplayModeDesc({kMaximumDisplayModeAxis, kMaximumDisplayModeAxis,
                                            kMaximumDisplayModeRefreshRate}));
}
TEST(DisplayModeValidationTest, AcceptsZeroRefreshAsBackendAutoMode) {
    EXPECT_TRUE(ValidateDisplayModeDesc({1280U, 720U, 0U}));
}
TEST(DisplayModeValidationTest, RejectsZeroOrOversizedDimensions) {
    EXPECT_FALSE(ValidateDisplayModeDesc({0U, 1080U, 60U}));
    EXPECT_FALSE(ValidateDisplayModeDesc({1920U, 0U, 60U}));
    EXPECT_FALSE(ValidateDisplayModeDesc({kMaximumDisplayModeAxis + 1U, 1080U, 60U}));
    EXPECT_FALSE(ValidateDisplayModeDesc({1920U, kMaximumDisplayModeAxis + 1U, 60U}));
}
TEST(DisplayModeValidationTest, RejectsExcessiveRefreshRate) {
    EXPECT_FALSE(ValidateDisplayModeDesc({1920U, 1080U, kMaximumDisplayModeRefreshRate + 1U}));
}

TEST(WindowModePolicyTest, FallsBackToWindowedOnlyWhenFullscreenLacksMonitorMode) {
    using Platform::WindowMode;
    for (const WindowMode mode : {WindowMode::Windowed, WindowMode::Maximized,
                                     WindowMode::Borderless, WindowMode::Fullscreen,
                                     WindowMode::ExclusiveFullscreen}) {
        EXPECT_EQ(ResolveInitialWindowMode(mode, true), mode);
    }

    EXPECT_EQ(ResolveInitialWindowMode(WindowMode::Windowed, false), WindowMode::Windowed);
    EXPECT_EQ(ResolveInitialWindowMode(WindowMode::Maximized, false), WindowMode::Maximized);
    EXPECT_EQ(ResolveInitialWindowMode(WindowMode::Borderless, false), WindowMode::Borderless);
    EXPECT_EQ(ResolveInitialWindowMode(WindowMode::Fullscreen, false), WindowMode::Windowed);
    EXPECT_EQ(ResolveInitialWindowMode(WindowMode::ExclusiveFullscreen, false), WindowMode::Windowed);
}

TEST(WindowModePolicyTest, ClassifiesOnlyMonitorFullscreenModesAsFullscreen) {
    using Platform::WindowMode;
    EXPECT_TRUE(IsFullscreenWindowMode(WindowMode::Fullscreen));
    EXPECT_TRUE(IsFullscreenWindowMode(WindowMode::ExclusiveFullscreen));
    EXPECT_FALSE(IsFullscreenWindowMode(WindowMode::Windowed));
    EXPECT_FALSE(IsFullscreenWindowMode(WindowMode::Maximized));
    EXPECT_FALSE(IsFullscreenWindowMode(WindowMode::Borderless));
}

TEST(WindowModePolicyTest, PreservesRestoreBoundsWhileMaximizedOrFullscreen) {
    EXPECT_TRUE(ShouldCaptureWindowedGeometry(false, false));
    EXPECT_FALSE(ShouldCaptureWindowedGeometry(false, true));
    EXPECT_FALSE(ShouldCaptureWindowedGeometry(true, false));
    EXPECT_FALSE(ShouldCaptureWindowedGeometry(true, true));
}
} // namespace
} // namespace UVE::Window::Tests
