// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "window_desc_validation.h"
#include "display_mode_validation.h"

#include <limits>
#include <string>
#include <vector>
#include <gtest/gtest.h>
namespace UVE::Window::Tests {
namespace {
TEST(WindowDescValidationTest, DefaultDescriptor_IsValidAndPreservesPortableWindowDefaults) {
    const WindowDesc desc{};
    EXPECT_TRUE(ValidateWindowDesc(desc));
    EXPECT_EQ(desc.title, "UniVex Engine");
    EXPECT_EQ(desc.width, 1280U);
    EXPECT_EQ(desc.height, 720U);
    EXPECT_TRUE(desc.resizable);
    EXPECT_TRUE(desc.vsyncEnabled);
    EXPECT_FALSE(desc.vsyncModeExplicit);
    EXPECT_EQ(desc.vsyncMode, Platform::VSyncMode::On);
    EXPECT_EQ(desc.mode, Platform::WindowMode::Windowed);
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
    EXPECT_EQ(desc.stretchMode, Platform::StretchMode::Disabled);
    EXPECT_EQ(desc.aspectPolicy, Platform::AspectPolicy::Keep);
    EXPECT_FALSE(desc.integerOnlyScaling);
    EXPECT_EQ(desc.orientation, Platform::DisplayOrientation::Auto);
    EXPECT_EQ(desc.allowedOrientations,
              (std::vector<Platform::DisplayOrientation>{Platform::DisplayOrientation::Landscape,
                                                            Platform::DisplayOrientation::Portrait}));
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
TEST(WindowDescValidationTest, ValidCustomDescriptor_IsAccepted) {
    WindowDesc desc;
    desc.title = "Editor Preview";
    desc.width = 1920U;
    desc.height = 1080U;
    desc.glVersionMajor = 3U;
    desc.glVersionMinor = 3U;
    EXPECT_TRUE(ValidateWindowDesc(desc));
}
TEST(WindowDescValidationTest, ZeroDimensionsOrEmptyTitle_AreRejected) {
    WindowDesc desc;
    desc.width = 0U;
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc.width = 1280U;
    desc.height = 0U;
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc.height = 720U;
    desc.title.clear();
    EXPECT_FALSE(ValidateWindowDesc(desc));
}
TEST(WindowDescValidationTest, AxisDimensionsAboveSharedCap_AreRejected) {
    WindowDesc desc;
    desc.width = kMaximumDisplayModeAxis + 1U;
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc.width = 1280U;
    desc.height = kMaximumDisplayModeAxis + 1U;
    EXPECT_FALSE(ValidateWindowDesc(desc));
}
TEST(WindowDescValidationTest, TitleAtSharedCap_IsAccepted) {
    WindowDesc desc;
    desc.title.assign(kMaximumWindowTitleBytes, 'T');
    EXPECT_TRUE(ValidateWindowDesc(desc));
}

TEST(WindowDescValidationTest, OversizedOrEmbeddedNulTitle_IsRejected) {
    WindowDesc desc;
    desc.title.assign(kMaximumWindowTitleBytes + 1U, 'T');
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc.title = std::string{"UVE\0Editor", 10U};
    EXPECT_FALSE(ValidateWindowDesc(desc));
}

TEST(WindowDescValidationTest, OpenGlMajorVersionBelowOne_IsRejected) {
    WindowDesc desc;
    desc.glVersionMajor = 0U;
    EXPECT_FALSE(ValidateWindowDesc(desc));
}

TEST(WindowDescValidationTest, SizeLimitsAndScalingPoliciesAreValidated) {
    WindowDesc desc;
    desc.minimumWidth = 1600U;
    desc.maximumWidth = 800U;
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc = WindowDesc{};
    desc.contentScaleOverride = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc.contentScaleOverride = 4.0;
    EXPECT_TRUE(ValidateWindowDesc(desc));
    desc.contentScaleOverride = 4.01;
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc.contentScaleOverride = -0.01;
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc = WindowDesc{};
    desc.mode = static_cast<Platform::WindowMode>(255U);
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc = WindowDesc{};
    desc.vsyncMode = static_cast<Platform::VSyncMode>(255U);
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc = WindowDesc{};
    desc.stretchMode = static_cast<Platform::StretchMode>(255U);
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc = WindowDesc{};
    desc.aspectPolicy = static_cast<Platform::AspectPolicy>(255U);
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc = WindowDesc{};
    desc.orientation = static_cast<Platform::DisplayOrientation>(255U);
    EXPECT_FALSE(ValidateWindowDesc(desc));
}

TEST(WindowDescValidationTest, PositionMonitorAndFrameRatePoliciesAreBounded) {
    WindowDesc desc;
    desc.initialPositionX = -131073;
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc.initialPositionX = -131072;
    EXPECT_TRUE(ValidateWindowDesc(desc));
    desc.initialPositionX = 131073;
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc = WindowDesc{};
    desc.initialPositionY = 131073;
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc = WindowDesc{};
    desc.monitorName.assign(257U, 'm');
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc.monitorName = "A";
    desc.monitorName.push_back('\0');
    desc.monitorName.push_back('B');
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc = WindowDesc{};
    desc.focusedFrameRateCap = 1000U;
    desc.unfocusedFrameRateCap = 1000U;
    EXPECT_TRUE(ValidateWindowDesc(desc));
    desc.focusedFrameRateCap = 1001U;
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc.focusedFrameRateCap = 0U;
    desc.unfocusedFrameRateCap = 1001U;
    EXPECT_FALSE(ValidateWindowDesc(desc));
}

TEST(WindowDescValidationTest, AllowedOrientationsMustBeUniqueAndContainRequestedOrientation) {
    WindowDesc desc;
    desc.allowedOrientations.clear();
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc = WindowDesc{};
    desc.allowedOrientations = {Platform::DisplayOrientation::Landscape,
                                Platform::DisplayOrientation::Landscape};
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc = WindowDesc{};
    desc.orientation = Platform::DisplayOrientation::LandscapeLeft;
    EXPECT_FALSE(ValidateWindowDesc(desc));
    desc.allowedOrientations.push_back(Platform::DisplayOrientation::LandscapeLeft);
    EXPECT_TRUE(ValidateWindowDesc(desc));
    desc.allowedOrientations.push_back(Platform::DisplayOrientation::Auto);
    EXPECT_FALSE(ValidateWindowDesc(desc));
}

TEST(WindowDescValidationTest, CursorPixelsAndHotspotMustMatch) {
    WindowDesc desc;
    desc.cursorImageWidth = 2U;
    desc.cursorImageHeight = 2U;
    desc.cursorRgba8.assign(16U, 255U);
    desc.cursorHotspotX = 1U;
    desc.cursorHotspotY = 1U;
    EXPECT_TRUE(ValidateWindowDesc(desc));

    desc.cursorHotspotX = 2U;
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc.cursorHotspotX = 1U;
    desc.cursorRgba8.pop_back();
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc = WindowDesc{};
    desc.cursorImageWidth = 4097U;
    desc.cursorImageHeight = 4096U;
    desc.cursorRgba8 = {255U};
    EXPECT_FALSE(ValidateWindowDesc(desc)); // Exceeds the 64 MiB pixel-data cap.
}

TEST(WindowDescValidationTest, IconsRequireBoundedDimensionsAndCompletePixelData) {
    WindowDesc desc;
    desc.icons.push_back(WindowIcon{1U, 1U, {0U, 1U, 2U, 3U}});
    EXPECT_TRUE(ValidateWindowDesc(desc));

    desc.icons.back().rgba8.pop_back();
    EXPECT_FALSE(ValidateWindowDesc(desc));

    desc = WindowDesc{};
    desc.icons.push_back(WindowIcon{4097U, 4096U, {}});
    EXPECT_FALSE(ValidateWindowDesc(desc)); // Rejected before allocating a 64 MiB+ payload.

    desc = WindowDesc{};
    desc.icons.resize(17U, WindowIcon{1U, 1U, {0U, 1U, 2U, 3U}});
    EXPECT_FALSE(ValidateWindowDesc(desc));
}
} // namespace
} // namespace UVE::Window::Tests
