// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "presentation_layout.h"
#include "window_title_format.h"

#include <gtest/gtest.h>

namespace UVE::Window::Tests {
namespace {

TEST(PresentationLayoutTest, KeepAspectLetterboxesAndCentersTheReferenceViewport) {
    const PresentationLayout layout = ComputePresentationLayout(
        1024U, 768U, 1280U, 720U, Platform::AspectPolicy::Keep, false);

    EXPECT_EQ(layout.x, 0U);
    EXPECT_EQ(layout.y, 96U);
    EXPECT_EQ(layout.width, 1024U);
    EXPECT_EQ(layout.height, 576U);
    EXPECT_DOUBLE_EQ(layout.logicalWidth, 1280.0);
    EXPECT_DOUBLE_EQ(layout.logicalHeight, 720.0);
    EXPECT_DOUBLE_EQ(layout.scaleX, 0.8);
    EXPECT_DOUBLE_EQ(layout.scaleY, 0.8);
}

TEST(PresentationLayoutTest, IntegerScalingUsesWholeNumberMagnification) {
    const PresentationLayout layout = ComputePresentationLayout(
        2560U, 1440U, 1280U, 720U, Platform::AspectPolicy::Keep, true);

    EXPECT_TRUE(layout.integerScaleAvailable);
    EXPECT_EQ(layout.width, 2560U);
    EXPECT_EQ(layout.height, 1440U);
    EXPECT_DOUBLE_EQ(layout.scaleX, 2.0);
    EXPECT_DOUBLE_EQ(layout.scaleY, 2.0);
}

TEST(PresentationLayoutTest, IntegerScalingReportsWhenOutputIsSmallerThanReference) {
    const PresentationLayout layout = ComputePresentationLayout(
        640U, 360U, 1280U, 720U, Platform::AspectPolicy::Keep, true);

    EXPECT_FALSE(layout.integerScaleAvailable);
    EXPECT_EQ(layout.width, 640U);
    EXPECT_EQ(layout.height, 360U);
}

TEST(PresentationLayoutTest, IntegerFallbackPreservesAspectWhenFractionalFitIsRequired) {
    const PresentationLayout layout = ComputePresentationLayout(
        640U, 480U, 1280U, 720U, Platform::AspectPolicy::Keep, true);

    EXPECT_FALSE(layout.integerScaleAvailable);
    EXPECT_EQ(layout.x, 0U);
    EXPECT_EQ(layout.y, 60U);
    EXPECT_EQ(layout.width, 640U);
    EXPECT_EQ(layout.height, 360U);
    EXPECT_DOUBLE_EQ(layout.scaleX, 0.5);
    EXPECT_DOUBLE_EQ(layout.scaleY, 0.5);
}

TEST(PresentationLayoutTest, ExpandAddsLogicalSpaceWithoutChangingPhysicalAspect) {
    const PresentationLayout layout = ComputePresentationLayout(
        1920U, 1080U, 1280U, 1024U, Platform::AspectPolicy::Expand, false);

    EXPECT_EQ(layout.width, 1920U);
    EXPECT_EQ(layout.height, 1080U);
    EXPECT_DOUBLE_EQ(layout.logicalHeight, 1024.0);
    EXPECT_NEAR(layout.logicalWidth, 1024.0 * (1920.0 / 1080.0), 1e-9);
    EXPECT_NEAR(layout.scaleX, layout.scaleY, 1e-9);
}

TEST(PresentationLayoutTest, KeepWidthAndKeepHeightExpandLogicalSpaceAlongOneAxis) {
    const PresentationLayout keepWidth = ComputePresentationLayout(
        1920U, 1080U, 1280U, 720U, Platform::AspectPolicy::KeepWidth, false);
    const PresentationLayout keepHeight = ComputePresentationLayout(
        1920U, 1080U, 1280U, 720U, Platform::AspectPolicy::KeepHeight, false);

    EXPECT_DOUBLE_EQ(keepWidth.logicalWidth, 1280.0);
    EXPECT_DOUBLE_EQ(keepWidth.logicalHeight, 720.0);
    EXPECT_DOUBLE_EQ(keepHeight.logicalWidth, 1280.0);
    EXPECT_DOUBLE_EQ(keepHeight.logicalHeight, 720.0);

    const PresentationLayout widerKeepWidth = ComputePresentationLayout(
        2560U, 1080U, 1280U, 720U, Platform::AspectPolicy::KeepWidth, false);
    const PresentationLayout widerKeepHeight = ComputePresentationLayout(
        2560U, 1080U, 1280U, 720U, Platform::AspectPolicy::KeepHeight, false);
    EXPECT_DOUBLE_EQ(widerKeepWidth.logicalWidth, 1280.0);
    EXPECT_LT(widerKeepWidth.logicalHeight, 720.0);
    EXPECT_GT(widerKeepHeight.logicalWidth, 1280.0);
    EXPECT_DOUBLE_EQ(widerKeepHeight.logicalHeight, 720.0);
}

TEST(PresentationLayoutTest, IntegerOnlyScalingAppliesToIgnoreAndKeepWidthPolicies) {
    const PresentationLayout ignored = ComputePresentationLayout(
        2560U, 1080U, 1280U, 720U, Platform::AspectPolicy::Ignore, true);
    EXPECT_TRUE(ignored.integerScaleAvailable);
    EXPECT_EQ(ignored.width, 2560U);
    EXPECT_EQ(ignored.height, 720U);
    EXPECT_EQ(ignored.x, 0U);
    EXPECT_EQ(ignored.y, 180U);
    EXPECT_DOUBLE_EQ(ignored.scaleX, 2.0);
    EXPECT_DOUBLE_EQ(ignored.scaleY, 1.0);

    const PresentationLayout keepWidth = ComputePresentationLayout(
        1920U, 1080U, 1280U, 720U, Platform::AspectPolicy::KeepWidth, true);
    EXPECT_TRUE(keepWidth.integerScaleAvailable);
    EXPECT_EQ(keepWidth.width, 1280U);
    EXPECT_EQ(keepWidth.height, 720U);
    EXPECT_EQ(keepWidth.x, 320U);
    EXPECT_EQ(keepWidth.y, 180U);
    EXPECT_DOUBLE_EQ(keepWidth.scaleX, 1.0);
    EXPECT_DOUBLE_EQ(keepWidth.scaleY, 1.0);
}

TEST(WindowTitleFormatTest, ReplacesProductProjectAndSceneTokens) {
    EXPECT_EQ(FormatWindowTitle("{projectName} / {productName} / {sceneName}", "Product",
                                  "Project", "MainHall", true, true),
              "Project / Product / MainHall");
}

TEST(WindowTitleFormatTest, AppendsSceneOnlyDuringEnabledEditorPlayMode) {
    EXPECT_EQ(FormatWindowTitle("{productName}", "Product", "Project", "MainHall", true, true),
              "Product - MainHall");
    EXPECT_EQ(FormatWindowTitle("{productName}", "Product", "Project", "MainHall", false, true),
              "Product");
    EXPECT_EQ(FormatWindowTitle("{productName}", "Product", "Project", "MainHall", true, false),
              "Product");
    EXPECT_EQ(FormatWindowTitle("{productName} ({sceneName})", "Product", "Project", "MainHall", true, true),
              "Product (MainHall)");
    EXPECT_EQ(FormatWindowTitle("{projectName} - {sceneName}", "Product", "Project", "MainHall", false, true),
              "Project");
    EXPECT_EQ(FormatWindowTitle("{projectName} ({sceneName})", "Product", "Project", "", false, true),
              "Project");
}

TEST(WindowTitleFormatTest, UnknownTokensArePreservedAndUtf8TruncationIsBounded) {
    EXPECT_EQ(FormatWindowTitle("{unknown}", "Product", "Project", "", false, false), "{unknown}");
    std::string longUtf8;
    while (longUtf8.size() <= kMaximumWindowTitleBytes) {
        longUtf8 += "\xE2\x82\xAC";
    }
    const std::string result = FormatWindowTitle(longUtf8, "Product", "Project", "", false, false);
    EXPECT_LE(result.size(), kMaximumWindowTitleBytes);
    EXPECT_EQ(result.size() % 3U, 0U);
}

} // namespace
} // namespace UVE::Window::Tests
