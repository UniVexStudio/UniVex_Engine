// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "accessibility_bridge.h"
#include "ui_rich_text.h"
#include "ui_runtime.h"

namespace UVE::UI {
namespace {

TEST(RichTextTest, ParsesNestedColorBoldItalicAndSizeSpans) {
    const auto spans = ParseRichTextSpans(
        "Hello [color=#ff0000][b]World[/b][/color] [size=24][i]AAA[/i][/size] [unknown]",
        Math::Vector3{1.0F, 1.0F, 1.0F}, 16.0F);
    ASSERT_GE(spans.size(), 4U);
    EXPECT_EQ(spans[0].text, "Hello ");
    EXPECT_FALSE(spans[0].bold);
    EXPECT_EQ(spans[1].text, "World");
    EXPECT_TRUE(spans[1].bold);
    EXPECT_NEAR(spans[1].color.x, 1.0F, 0.01F);
    EXPECT_NEAR(spans[1].color.y, 0.0F, 0.01F);

    bool foundItalic24 = false;
    for (const auto& sp : spans) {
        if (sp.text == "AAA") {
            foundItalic24 = sp.italic && (sp.fontSize == 24.0F);
        }
    }
    EXPECT_TRUE(foundItalic24);
}

TEST(UnicodeRtlLocalizationTest, DecodesUtf8ReordersRtlAndResolvesFontFallbacks) {
    // Arabic word "مرحبا" + embedded LTR number "2026"
    const std::string arabic = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 2026";
    const auto cps = DecodeUtf8Codepoints(arabic);
    ASSERT_EQ(cps.size(), 10U);
    EXPECT_TRUE(IsRtlCodepoint(cps[0]));
    EXPECT_EQ(EncodeUtf8Codepoints(cps), arabic);

    const std::string visual = ResolveVisualTextOrder(arabic, Scene::UITextDirection::Auto);
    // Embedded number "2026" remains readable left-to-right at the start of the visual RTL line.
    EXPECT_EQ(visual.rfind("2026", 0U), 0U);

    UIFontFallbackChain chain;
    const std::size_t latinIdx = chain.ResolveFontIndexForCodepoint(U'A');
    const std::size_t cyrillicIdx = chain.ResolveFontIndexForCodepoint(0x0416U);
    const std::size_t arabicIdx = chain.ResolveFontIndexForCodepoint(0x0645U);
    const std::size_t cjkIdx = chain.ResolveFontIndexForCodepoint(0x6F22U);
    EXPECT_EQ(latinIdx, 0U);
    EXPECT_NE(cyrillicIdx, latinIdx);
    EXPECT_NE(arabicIdx, latinIdx);
    EXPECT_NE(cjkIdx, latinIdx);
}

TEST(AccessibilityTest, DaltonizationScalingAndScreenReaderBridgeWork) {
    const Math::Vector3 red{0.9F, 0.1F, 0.1F};
    const Math::Vector3 none = ApplyColorblindDaltonizationCpu(red, ColorblindMode::None, 1.0F);
    EXPECT_FLOAT_EQ(none.x, red.x);
    EXPECT_FLOAT_EQ(none.y, red.y);
    EXPECT_FLOAT_EQ(none.z, red.z);

    const Math::Vector3 protan =
        ApplyColorblindDaltonizationCpu(red, ColorblindMode::Protanopia, 1.0F);
    const Math::Vector3 deutan =
        ApplyColorblindDaltonizationCpu(red, ColorblindMode::Deuteranopia, 1.0F);
    const Math::Vector3 tritan =
        ApplyColorblindDaltonizationCpu(red, ColorblindMode::Tritanopia, 1.0F);
    EXPECT_NE(protan.y, red.y);
    EXPECT_NE(deutan.z, red.z);
    EXPECT_GE(tritan.x, 0.0F);
    EXPECT_LE(tritan.z, 1.0F);

    RecordingAccessibilityBridge bridge;
    UIRuntime ui;
    ui.SetUiScaleFactor(1.5F);
    EXPECT_FLOAT_EQ(ui.GetUiScaleFactor(), 1.5F);
    ui.SetAccessibilityBridge(&bridge);
    EXPECT_EQ(ui.GetAccessibilityBridge(), &bridge);

    bridge.AnnounceFocus(AccessibilityNodeInfo{Scene::Entity{1U, 1U}, "button", "Play", "", true, true});
    bridge.AnnounceText("Game Saved");
    ASSERT_EQ(bridge.FocusEvents().size(), 1U);
    EXPECT_EQ(bridge.FocusEvents()[0].label, "Play");
    ASSERT_EQ(bridge.Announcements().size(), 1U);
}

} // namespace
} // namespace UVE::UI
