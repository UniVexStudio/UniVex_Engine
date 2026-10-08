// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <uve/window/window_size_policy_uve.h>

namespace UVE::Window::Tests {
namespace {

TEST(WindowSizePolicyUVETest, ZeroLimitsResolveToBackendDontCareSentinel) {
    EXPECT_EQ(ResolveWindowSizeLimitsUVE(0U, 0U, 0U, 0U, -1),
              (WindowSizeLimitsUVE{-1, -1, -1, -1}));
}

TEST(WindowSizePolicyUVETest, MinimumAndMaximumLimitsResolvePerAxisIndependently) {
    EXPECT_EQ(ResolveWindowSizeLimitsUVE(640U, 360U, 1920U, 1080U, -1),
              (WindowSizeLimitsUVE{640, 360, 1920, 1080}));
    EXPECT_EQ(ResolveWindowSizeLimitsUVE(800U, 0U, 0U, 900U, -1),
              (WindowSizeLimitsUVE{800, -1, -1, 900}));
}

TEST(WindowSizePolicyUVETest, CustomBackendSentinelIsPreserved) {
    EXPECT_EQ(ResolveWindowSizeLimitsUVE(0U, 480U, 0U, 0U, 0),
              (WindowSizeLimitsUVE{0, 480, 0, 0}));
}

} // namespace
} // namespace UVE::Window::Tests
