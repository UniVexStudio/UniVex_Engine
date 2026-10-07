// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <uve/window/window_style_policy_uve.h>

namespace UVE::Window::Tests {
namespace {

TEST(WindowStylePolicyUVETest, OrdinaryWindowedAndMaximizedModesKeepDecorations) {
    EXPECT_TRUE(ShouldDecorateWindowUVE(false, Platform::WindowModeUVE::Windowed));
    EXPECT_TRUE(ShouldDecorateWindowUVE(false, Platform::WindowModeUVE::Maximized));
}

TEST(WindowStylePolicyUVETest, BorderlessModeRemovesDecorations) {
    EXPECT_FALSE(ShouldDecorateWindowUVE(false, Platform::WindowModeUVE::Borderless));
}

TEST(WindowStylePolicyUVETest, BothFullscreenModesAreUndecorated) {
    EXPECT_FALSE(ShouldDecorateWindowUVE(false, Platform::WindowModeUVE::Fullscreen));
    EXPECT_FALSE(ShouldDecorateWindowUVE(false, Platform::WindowModeUVE::ExclusiveFullscreen));
}

TEST(WindowStylePolicyUVETest, BorderlessSettingRemovesDecorationsFromWindowedModes) {
    EXPECT_FALSE(ShouldDecorateWindowUVE(true, Platform::WindowModeUVE::Windowed));
    EXPECT_FALSE(ShouldDecorateWindowUVE(true, Platform::WindowModeUVE::Maximized));
}

} // namespace
} // namespace UVE::Window::Tests
