// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/window/presentation_layout_uve.h"
#include "uve/window/window_title_format_uve.h"

#include <gtest/gtest.h>

namespace UVE::Window::Tests {
namespace {

TEST(PresentationLayoutUVETest, KeepAspectLetterboxesAndCentersTheReferenceViewport) {
    const PresentationLayoutUVE layout = ComputePresentationLayoutUVE(
        1024U, 768U, 1280U, 720U, Platform::AspectPolicyUVE::Keep, false);

    EXPECT_EQ(layout.x, 0U);
    EXPECT_EQ(layout.y, 96U);
    EXPECT_EQ(layout.width, 1024U);
    EXPECT_EQ(layout.height, 576U);
    EXPECT_DOUBLE_EQ(layout.logicalWidth, 1280.0);
    EXPECT_DOUBLE_EQ(layout.logicalHeight, 720.0);
    EXPECT_DOUBLE_EQ(layout.scaleX, 0.8);
    EXPECT_DOUBLE_EQ(layout.scaleY, 0.8);
}

TEST(PresentationLayoutUVETest, IntegerScalingUsesWholeNumberMagnification) {
    const PresentationLayoutUVE layout = ComputePresentationLayoutUVE(
        2560U, 1440U, 1280U, 720U, Platform::AspectPolicyUVE::Keep, true);

    EXPECT_TRUE(layout.integerScaleAvailable);
    EXPECT_EQ(layout.width, 2560U);
    EXPECT_EQ(layout.height, 1440U);
    EXPECT_DOUBLE_EQ(layout.scaleX, 2.0);
    EXPECT_DOUBLE_EQ(layout.scaleY, 2.0);
}

TEST(PresentationLayoutUVETest, IntegerScalingReportsWhenOutputIsSmallerThanReference) {
    const PresentationLayoutUVE layout = ComputePresentationLayoutUVE(
        640U, 360U, 1280U, 720U, Platform::AspectPolicyUVE::Keep, true);

    EXPECT_FALSE(layout.integerScaleAvailable);
    EXPECT_EQ(layout.width, 640U);
    EXPECT_EQ(layout.height, 360U);
}

TEST(PresentationLayoutUVETest, IntegerFallbackPreservesAspectWhenFractionalFitIsRequired) {
    const PresentationLayoutUVE layout = ComputePresentationLayoutUVE(
        640U, 480U, 1280U, 720U, Platform::AspectPolicyUVE::Keep, true);

    EXPECT_FALSE(layout.integerScaleAvailable);
    EXPECT_EQ(layout.x, 0U);
    EXPECT_EQ(layout.y, 60U);
    EXPECT_EQ(layout.width, 640U);
    EXPECT_EQ(layout.height, 360U);
    EXPECT_DOUBLE_EQ(layout.scaleX, 0.5);
    EXPECT_DOUBLE_EQ(layout.scaleY, 0.5);
}

TEST(PresentationLayoutUVETest, ExpandAddsLogicalSpaceWithoutChangingPhysicalAspect) {
    const PresentationLayoutUVE layout = ComputePresentationLayoutUVE(
        1920U, 1080U, 1280U, 1024U, Platform::AspectPolicyUVE::Expand, false);

    EXPECT_EQ(layout.width, 1920U);
    EXPECT_EQ(layout.height, 1080U);
    EXPECT_DOUBLE_EQ(layout.logicalHeight, 1024.0);
    EXPECT_NEAR(layout.logicalWidth, 1024.0 * (1920.0 / 1080.0), 1e-9);
    EXPECT_NEAR(layout.scaleX, layout.scaleY, 1e-9);
}

TEST(PresentationLayoutUVETest, KeepWidthAndKeepHeightExpandLogicalSpaceAlongOneAxis) {
    const PresentationLayoutUVE keepWidth = ComputePresentationLayoutUVE(
        1920U, 1080U, 1280U, 720U, Platform::AspectPolicyUVE::KeepWidth, false);
    const PresentationLayoutUVE keepHeight = ComputePresentationLayoutUVE(
        1920U, 1080U, 1280U, 720U, Platform::AspectPolicyUVE::KeepHeight, false);

    EXPECT_DOUBLE_EQ(keepWidth.logicalWidth, 1280.0);
    EXPECT_DOUBLE_EQ(keepWidth.logicalHeight, 720.0);
    EXPECT_DOUBLE_EQ(keepHeight.logicalWidth, 1280.0);
    EXPECT_DOUBLE_EQ(keepHeight.logicalHeight, 720.0);

    const PresentationLayoutUVE widerKeepWidth = ComputePresentationLayoutUVE(
        2560U, 1080U, 1280U, 720U, Platform::AspectPolicyUVE::KeepWidth, false);
    const PresentationLayoutUVE widerKeepHeight = ComputePresentationLayoutUVE(
        2560U, 1080U, 1280U, 720U, Platform::AspectPolicyUVE::KeepHeight, false);
    EXPECT_DOUBLE_EQ(widerKeepWidth.logicalWidth, 1280.0);
    EXPECT_LT(widerKeepWidth.logicalHeight, 720.0);
    EXPECT_GT(widerKeepHeight.logicalWidth, 1280.0);
    EXPECT_DOUBLE_EQ(widerKeepHeight.logicalHeight, 720.0);
}

TEST(PresentationLayoutUVETest, IntegerOnlyScalingAppliesToIgnoreAndKeepWidthPolicies) {
    const PresentationLayoutUVE ignored = ComputePresentationLayoutUVE(
        2560U, 1080U, 1280U, 720U, Platform::AspectPolicyUVE::Ignore, true);
    EXPECT_TRUE(ignored.integerScaleAvailable);
    EXPECT_EQ(ignored.width, 2560U);
    EXPECT_EQ(ignored.height, 720U);
    EXPECT_EQ(ignored.x, 0U);
    EXPECT_EQ(ignored.y, 180U);
    EXPECT_DOUBLE_EQ(ignored.scaleX, 2.0);
    EXPECT_DOUBLE_EQ(ignored.scaleY, 1.0);

    const PresentationLayoutUVE keepWidth = ComputePresentationLayoutUVE(
        1920U, 1080U, 1280U, 720U, Platform::AspectPolicyUVE::KeepWidth, true);
    EXPECT_TRUE(keepWidth.integerScaleAvailable);
    EXPECT_EQ(keepWidth.width, 1280U);
    EXPECT_EQ(keepWidth.height, 720U);
    EXPECT_EQ(keepWidth.x, 320U);
    EXPECT_EQ(keepWidth.y, 180U);
    EXPECT_DOUBLE_EQ(keepWidth.scaleX, 1.0);
    EXPECT_DOUBLE_EQ(keepWidth.scaleY, 1.0);
}

TEST(WindowTitleFormatUVETest, ReplacesProductProjectAndSceneTokens) {
    EXPECT_EQ(FormatWindowTitleUVE("{projectName} / {productName} / {sceneName}", "Product",
                                  "Project", "MainHall", true, true),
              "Project / Product / MainHall");
}

TEST(WindowTitleFormatUVETest, AppendsSceneOnlyDuringEnabledEditorPlayMode) {
    EXPECT_EQ(FormatWindowTitleUVE("{productName}", "Product", "Project", "MainHall", true, true),
              "Product - MainHall");
    EXPECT_EQ(FormatWindowTitleUVE("{productName}", "Product", "Project", "MainHall", false, true),
              "Product");
    EXPECT_EQ(FormatWindowTitleUVE("{productName}", "Product", "Project", "MainHall", true, false),
              "Product");
    EXPECT_EQ(FormatWindowTitleUVE("{productName} ({sceneName})", "Product", "Project", "MainHall", true, true),
              "Product (MainHall)");
    EXPECT_EQ(FormatWindowTitleUVE("{projectName} - {sceneName}", "Product", "Project", "MainHall", false, true),
              "Project");
    EXPECT_EQ(FormatWindowTitleUVE("{projectName} ({sceneName})", "Product", "Project", "", false, true),
              "Project");
}

TEST(WindowTitleFormatUVETest, UnknownTokensArePreservedAndUtf8TruncationIsBounded) {
    EXPECT_EQ(FormatWindowTitleUVE("{unknown}", "Product", "Project", "", false, false), "{unknown}");
    std::string longUtf8;
    while (longUtf8.size() <= kMaximumWindowTitleBytesUVE) {
        longUtf8 += "\xE2\x82\xAC";
    }
    const std::string result = FormatWindowTitleUVE(longUtf8, "Product", "Project", "", false, false);
    EXPECT_LE(result.size(), kMaximumWindowTitleBytesUVE);
    EXPECT_EQ(result.size() % 3U, 0U);
}

} // namespace
} // namespace UVE::Window::Tests
