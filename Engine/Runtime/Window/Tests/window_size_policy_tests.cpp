// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <window_size_policy.h>

namespace UVE::Window::Tests {
namespace {

TEST(WindowSizePolicyTest, ZeroLimitsResolveToBackendDontCareSentinel) {
    EXPECT_EQ(ResolveWindowSizeLimits(0U, 0U, 0U, 0U, -1),
              (WindowSizeLimits{-1, -1, -1, -1}));
}

TEST(WindowSizePolicyTest, MinimumAndMaximumLimitsResolvePerAxisIndependently) {
    EXPECT_EQ(ResolveWindowSizeLimits(640U, 360U, 1920U, 1080U, -1),
              (WindowSizeLimits{640, 360, 1920, 1080}));
    EXPECT_EQ(ResolveWindowSizeLimits(800U, 0U, 0U, 900U, -1),
              (WindowSizeLimits{800, -1, -1, 900}));
}

TEST(WindowSizePolicyTest, CustomBackendSentinelIsPreserved) {
    EXPECT_EQ(ResolveWindowSizeLimits(0U, 480U, 0U, 0U, 0),
              (WindowSizeLimits{0, 480, 0, 0}));
}

} // namespace
} // namespace UVE::Window::Tests
