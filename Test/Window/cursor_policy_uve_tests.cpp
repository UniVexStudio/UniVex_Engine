// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <uve/window/cursor_policy_uve.h>

namespace UVE::Window {
namespace {

TEST(CursorInputPolicyUVETest, VisibleUnconfinedCursorUsesNormalModeAndCustomImage) {
    EXPECT_EQ(ResolveCursorInputPolicyUVE(true, false),
              (CursorInputResolutionUVE{CursorInputModeUVE::Normal, true, false}));
}

TEST(CursorInputPolicyUVETest, HiddenUnconfinedCursorUsesHiddenModeWithoutCustomImage) {
    EXPECT_EQ(ResolveCursorInputPolicyUVE(false, false),
              (CursorInputResolutionUVE{CursorInputModeUVE::Hidden, false, false}));
}

TEST(CursorInputPolicyUVETest, VisibleConfinedCursorReportsGlfwVisibilityCompromise) {
    EXPECT_EQ(ResolveCursorInputPolicyUVE(true, true),
              (CursorInputResolutionUVE{CursorInputModeUVE::Confined, false, true}));
}

TEST(CursorInputPolicyUVETest, HiddenConfinedCursorDoesNotReportVisibilityCompromise) {
    EXPECT_EQ(ResolveCursorInputPolicyUVE(false, true),
              (CursorInputResolutionUVE{CursorInputModeUVE::Confined, false, false}));
}

} // namespace
} // namespace UVE::Window
