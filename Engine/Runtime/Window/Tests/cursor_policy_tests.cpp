// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <cursor_policy.h>

namespace UVE::Window {
namespace {

TEST(CursorInputPolicyTest, VisibleUnconfinedCursorUsesNormalModeAndCustomImage) {
    EXPECT_EQ(ResolveCursorInputPolicy(true, false),
              (CursorInputResolution{CursorInputMode::Normal, true, false}));
}

TEST(CursorInputPolicyTest, HiddenUnconfinedCursorUsesHiddenModeWithoutCustomImage) {
    EXPECT_EQ(ResolveCursorInputPolicy(false, false),
              (CursorInputResolution{CursorInputMode::Hidden, false, false}));
}

TEST(CursorInputPolicyTest, VisibleConfinedCursorReportsGlfwVisibilityCompromise) {
    EXPECT_EQ(ResolveCursorInputPolicy(true, true),
              (CursorInputResolution{CursorInputMode::Confined, false, true}));
}

TEST(CursorInputPolicyTest, HiddenConfinedCursorDoesNotReportVisibilityCompromise) {
    EXPECT_EQ(ResolveCursorInputPolicy(false, true),
              (CursorInputResolution{CursorInputMode::Confined, false, false}));
}

} // namespace
} // namespace UVE::Window
