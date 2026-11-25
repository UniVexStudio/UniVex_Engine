// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <window_style_policy.h>

namespace UVE::Window::Tests {
namespace {

TEST(WindowStylePolicyTest, OrdinaryWindowedAndMaximizedModesKeepDecorations) {
    EXPECT_TRUE(ShouldDecorateWindow(false, Platform::WindowMode::Windowed));
    EXPECT_TRUE(ShouldDecorateWindow(false, Platform::WindowMode::Maximized));
}

TEST(WindowStylePolicyTest, BorderlessModeRemovesDecorations) {
    EXPECT_FALSE(ShouldDecorateWindow(false, Platform::WindowMode::Borderless));
}

TEST(WindowStylePolicyTest, BothFullscreenModesAreUndecorated) {
    EXPECT_FALSE(ShouldDecorateWindow(false, Platform::WindowMode::Fullscreen));
    EXPECT_FALSE(ShouldDecorateWindow(false, Platform::WindowMode::ExclusiveFullscreen));
}

TEST(WindowStylePolicyTest, BorderlessSettingRemovesDecorationsFromWindowedModes) {
    EXPECT_FALSE(ShouldDecorateWindow(true, Platform::WindowMode::Windowed));
    EXPECT_FALSE(ShouldDecorateWindow(true, Platform::WindowMode::Maximized));
}

} // namespace
} // namespace UVE::Window::Tests
