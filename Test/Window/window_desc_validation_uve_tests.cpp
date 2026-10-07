// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/window/window_desc_validation_uve.h"
#include "uve/window/display_mode_validation_uve.h"

#include <limits>
#include <string>
#include <vector>
#include <gtest/gtest.h>
namespace UVE::Window::Tests {
namespace {
TEST(WindowDescValidationUVETest, DefaultDescriptor_IsValidAndPreservesPortableWindowDefaults) {
    const WindowDescUVE desc{};
    EXPECT_TRUE(ValidateWindowDescUVE(desc));
    EXPECT_EQ(desc.title, "UniVex Engine");
    EXPECT_EQ(desc.width, 1280U);
    EXPECT_EQ(desc.height, 720U);
    EXPECT_TRUE(desc.resizable);
    EXPECT_TRUE(desc.vsyncEnabled);
    EXPECT_FALSE(desc.vsyncModeExplicit);
    EXPECT_EQ(desc.vsyncMode, Platform::VSyncModeUVE::On);
    EXPECT_EQ(desc.mode, Platform::WindowModeUVE::Windowed);
    EXPECT_FALSE(desc.borderless);
    EXPECT_FALSE(desc.alwaysOnTop);
    EXPECT_FALSE(desc.transparent);
    EXPECT_EQ(desc.minimumWidth, 0U);
    EXPECT_EQ(desc.minimumHeight, 0U);
    EXPECT_EQ(desc.maximumWidth, 0U);
    EXPECT_EQ(desc.maximumHeight, 0U);
    EXPECT_FALSE(desc.initialPositionSpecified);
    EXPECT_EQ(desc.initialPositionX, 0);
    EXPECT_EQ(desc.initialPositionY, 0);
    EXPECT_TRUE(desc.monitorName.empty());
    EXPECT_TRUE(desc.highDpiAware);
    EXPECT_TRUE(desc.perMonitorScaling);
    EXPECT_DOUBLE_EQ(desc.contentScaleOverride, 0.0);
    EXPECT_EQ(desc.stretchMode, Platform::StretchModeUVE::Disabled);
    EXPECT_EQ(desc.aspectPolicy, Platform::AspectPolicyUVE::Keep);
    EXPECT_FALSE(desc.integerOnlyScaling);
    EXPECT_EQ(desc.orientation, Platform::DisplayOrientationUVE::Auto);
    EXPECT_EQ(desc.allowedOrientations,
              (std::vector<Platform::DisplayOrientationUVE>{Platform::DisplayOrientationUVE::Landscape,
                                                            Platform::DisplayOrientationUVE::Portrait}));
    EXPECT_EQ(desc.focusedFrameRateCap, 0U);
    EXPECT_EQ(desc.unfocusedFrameRateCap, 0U);
    EXPECT_TRUE(desc.allowDisplaySleep);
    EXPECT_TRUE(desc.cursorRgba8.empty());
    EXPECT_EQ(desc.cursorHotspotX, 0U);
    EXPECT_EQ(desc.cursorHotspotY, 0U);
    EXPECT_TRUE(desc.cursorVisible);
    EXPECT_FALSE(desc.cursorConfinedToWindow);
    EXPECT_EQ(desc.glVersionMajor, 4U);
    EXPECT_EQ(desc.glVersionMinor, 6U);
    EXPECT_TRUE(desc.icons.empty());
}
TEST(WindowDescValidationUVETest, ValidCustomDescriptor_IsAccepted) {
    WindowDescUVE desc;
    desc.title = "Editor Preview";
    desc.width = 1920U;
    desc.height = 1080U;
    desc.glVersionMajor = 3U;
    desc.glVersionMinor = 3U;
    EXPECT_TRUE(ValidateWindowDescUVE(desc));
}
TEST(WindowDescValidationUVETest, ZeroDimensionsOrEmptyTitle_AreRejected) {
    WindowDescUVE desc;
    desc.width = 0U;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc.width = 1280U;
    desc.height = 0U;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc.height = 720U;
    desc.title.clear();
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
}
TEST(WindowDescValidationUVETest, AxisDimensionsAboveSharedCap_AreRejected) {
    WindowDescUVE desc;
    desc.width = kMaximumDisplayModeAxisUVE + 1U;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc.width = 1280U;
    desc.height = kMaximumDisplayModeAxisUVE + 1U;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
}
TEST(WindowDescValidationUVETest, TitleAtSharedCap_IsAccepted) {
    WindowDescUVE desc;
    desc.title.assign(kMaximumWindowTitleBytesUVE, 'T');
    EXPECT_TRUE(ValidateWindowDescUVE(desc));
}

TEST(WindowDescValidationUVETest, OversizedOrEmbeddedNulTitle_IsRejected) {
    WindowDescUVE desc;
    desc.title.assign(kMaximumWindowTitleBytesUVE + 1U, 'T');
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc.title = std::string{"UVE\0Editor", 10U};
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
}

TEST(WindowDescValidationUVETest, OpenGlMajorVersionBelowOne_IsRejected) {
    WindowDescUVE desc;
    desc.glVersionMajor = 0U;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
}

TEST(WindowDescValidationUVETest, SizeLimitsAndScalingPoliciesAreValidated) {
    WindowDescUVE desc;
    desc.minimumWidth = 1600U;
    desc.maximumWidth = 800U;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc = WindowDescUVE{};
    desc.contentScaleOverride = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc.contentScaleOverride = 4.0;
    EXPECT_TRUE(ValidateWindowDescUVE(desc));
    desc.contentScaleOverride = 4.01;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc.contentScaleOverride = -0.01;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc = WindowDescUVE{};
    desc.mode = static_cast<Platform::WindowModeUVE>(255U);
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc = WindowDescUVE{};
    desc.vsyncMode = static_cast<Platform::VSyncModeUVE>(255U);
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc = WindowDescUVE{};
    desc.stretchMode = static_cast<Platform::StretchModeUVE>(255U);
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc = WindowDescUVE{};
    desc.aspectPolicy = static_cast<Platform::AspectPolicyUVE>(255U);
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc = WindowDescUVE{};
    desc.orientation = static_cast<Platform::DisplayOrientationUVE>(255U);
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
}

TEST(WindowDescValidationUVETest, PositionMonitorAndFrameRatePoliciesAreBounded) {
    WindowDescUVE desc;
    desc.initialPositionX = -131073;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc.initialPositionX = -131072;
    EXPECT_TRUE(ValidateWindowDescUVE(desc));
    desc.initialPositionX = 131073;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc = WindowDescUVE{};
    desc.initialPositionY = 131073;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc = WindowDescUVE{};
    desc.monitorName.assign(257U, 'm');
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc.monitorName = "A";
    desc.monitorName.push_back('\0');
    desc.monitorName.push_back('B');
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc = WindowDescUVE{};
    desc.focusedFrameRateCap = 1000U;
    desc.unfocusedFrameRateCap = 1000U;
    EXPECT_TRUE(ValidateWindowDescUVE(desc));
    desc.focusedFrameRateCap = 1001U;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc.focusedFrameRateCap = 0U;
    desc.unfocusedFrameRateCap = 1001U;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
}

TEST(WindowDescValidationUVETest, AllowedOrientationsMustBeUniqueAndContainRequestedOrientation) {
    WindowDescUVE desc;
    desc.allowedOrientations.clear();
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc = WindowDescUVE{};
    desc.allowedOrientations = {Platform::DisplayOrientationUVE::Landscape,
                                Platform::DisplayOrientationUVE::Landscape};
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc = WindowDescUVE{};
    desc.orientation = Platform::DisplayOrientationUVE::LandscapeLeft;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
    desc.allowedOrientations.push_back(Platform::DisplayOrientationUVE::LandscapeLeft);
    EXPECT_TRUE(ValidateWindowDescUVE(desc));
    desc.allowedOrientations.push_back(Platform::DisplayOrientationUVE::Auto);
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
}

TEST(WindowDescValidationUVETest, CursorPixelsAndHotspotMustMatch) {
    WindowDescUVE desc;
    desc.cursorImageWidth = 2U;
    desc.cursorImageHeight = 2U;
    desc.cursorRgba8.assign(16U, 255U);
    desc.cursorHotspotX = 1U;
    desc.cursorHotspotY = 1U;
    EXPECT_TRUE(ValidateWindowDescUVE(desc));

    desc.cursorHotspotX = 2U;
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc.cursorHotspotX = 1U;
    desc.cursorRgba8.pop_back();
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc = WindowDescUVE{};
    desc.cursorImageWidth = 4097U;
    desc.cursorImageHeight = 4096U;
    desc.cursorRgba8 = {255U};
    EXPECT_FALSE(ValidateWindowDescUVE(desc)); // Exceeds the 64 MiB pixel-data cap.
}

TEST(WindowDescValidationUVETest, IconsRequireBoundedDimensionsAndCompletePixelData) {
    WindowDescUVE desc;
    desc.icons.push_back(WindowIconUVE{1U, 1U, {0U, 1U, 2U, 3U}});
    EXPECT_TRUE(ValidateWindowDescUVE(desc));

    desc.icons.back().rgba8.pop_back();
    EXPECT_FALSE(ValidateWindowDescUVE(desc));

    desc = WindowDescUVE{};
    desc.icons.push_back(WindowIconUVE{4097U, 4096U, {}});
    EXPECT_FALSE(ValidateWindowDescUVE(desc)); // Rejected before allocating a 64 MiB+ payload.

    desc = WindowDescUVE{};
    desc.icons.resize(17U, WindowIconUVE{1U, 1U, {0U, 1U, 2U, 3U}});
    EXPECT_FALSE(ValidateWindowDescUVE(desc));
}
} // namespace
} // namespace UVE::Window::Tests
