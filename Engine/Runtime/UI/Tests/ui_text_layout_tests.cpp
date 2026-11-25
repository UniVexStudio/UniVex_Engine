// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "ui_text.h"
#include "ui_text_component.h"

namespace UVE::Scene {
namespace {

// Every character 10 pixels wide: the layout's arithmetic is then exact.
[[nodiscard]] float Measure(const std::string_view text) { return static_cast<float>(text.size()) * 10.0F; }

TEST(UITextLayoutTest, CentredTextSitsInTheMiddleOfItsBox) {
    UITextComponent text;
    text.text = "Start";
    text.fontSize = 20.0F;
    text.horizontalAlign = UITextHorizontalAlign::Center;
    text.verticalAlign = UITextVerticalAlign::Middle;
    const Math::Rect box{Math::Vector2{100.0F, 200.0F}, Math::Vector2{200.0F, 60.0F}};
    const std::vector<UIText::Line> lines = UIText::LayoutLines(text, text.text, box, Measure);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_FLOAT_EQ(lines[0].width, 50.0F);
    EXPECT_FLOAT_EQ(lines[0].baseline.x, 100.0F + (200.0F - 50.0F) * 0.5F);
    // The capitals' middle is the box's middle.
    const float capTop = lines[0].baseline.y - 20.0F * 0.72F;
    EXPECT_NEAR((capTop + lines[0].baseline.y) * 0.5F, 230.0F, 1e-4F);
}

TEST(UITextLayoutTest, RightAndBottomAlignToTheFarEdges) {
    UITextComponent text;
    text.text = "ab";
    text.fontSize = 10.0F;
    text.horizontalAlign = UITextHorizontalAlign::Right;
    text.verticalAlign = UITextVerticalAlign::Bottom;
    const Math::Rect box{Math::Vector2{0.0F, 0.0F}, Math::Vector2{100.0F, 40.0F}};
    const std::vector<UIText::Line> lines = UIText::LayoutLines(text, text.text, box, Measure);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_FLOAT_EQ(lines[0].baseline.x + lines[0].width, 100.0F);
    EXPECT_FLOAT_EQ(lines[0].baseline.y, 40.0F);
}

TEST(UITextLayoutTest, WrapBreaksAtSpacesToStayInsideTheBox) {
    UITextComponent text;
    text.text = "one two three";
    text.fontSize = 10.0F;
    text.wrap = true;
    const Math::Rect box{Math::Vector2{0.0F, 0.0F}, Math::Vector2{75.0F, 100.0F}};
    const std::vector<UIText::Line> lines = UIText::LayoutLines(text, text.text, box, Measure);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0].text, "one two");
    EXPECT_EQ(lines[1].text, "three");
    EXPECT_FLOAT_EQ(lines[1].baseline.y - lines[0].baseline.y, 10.0F * text.lineHeight);
}

TEST(UITextLayoutTest, WithoutABoxTheFirstBaselineIsAFontSizeBelowThePosition) {
    UITextComponent text;
    text.text = "Hi\nthere";
    text.fontSize = 16.0F;
    text.positionPixels = Math::Vector2{5.0F, 7.0F};
    const std::vector<UIText::Line> lines = UIText::LayoutLines(text, text.text, Math::Rect{}, Measure);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_FLOAT_EQ(lines[0].baseline.x, 5.0F);
    EXPECT_FLOAT_EQ(lines[0].baseline.y, 7.0F + 16.0F);
    EXPECT_EQ(lines[1].text, "there");
}

} // namespace
} // namespace UVE::Scene
