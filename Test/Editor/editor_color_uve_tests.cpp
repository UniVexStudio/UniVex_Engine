// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_color_uve.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

void ExpectColorNear(const EditorColorUVE& actual, const EditorColorUVE& expected, const float tolerance = 1e-5F) {
    EXPECT_NEAR(actual.r, expected.r, tolerance);
    EXPECT_NEAR(actual.g, expected.g, tolerance);
    EXPECT_NEAR(actual.b, expected.b, tolerance);
    EXPECT_NEAR(actual.a, expected.a, tolerance);
}

TEST(EditorColorUVETest, ParseColorHex_AcceptsThreeSixAndEightDigitForms) {
    // Six digits, with and without the marker, either case, spaces around it ignored.
    for (const char* text : {"#FF8000", "FF8000", "ff8000", "  #Ff8000 \t"}) {
        const std::optional<EditorColorUVE> color = ParseColorHexUVE(text);
        ASSERT_TRUE(color.has_value()) << text;
        ExpectColorNear(*color, EditorColorUVE{1.0F, 128.0F / 255.0F, 0.0F, 1.0F});
    }
    // Three digits double each one: F is FF, not F0.
    const std::optional<EditorColorUVE> shorthand = ParseColorHexUVE("#F80");
    ASSERT_TRUE(shorthand.has_value());
    ExpectColorNear(*shorthand, EditorColorUVE{1.0F, 136.0F / 255.0F, 0.0F, 1.0F});
    // Eight digits carry alpha.
    const std::optional<EditorColorUVE> withAlpha = ParseColorHexUVE("#00E07580");
    ASSERT_TRUE(withAlpha.has_value());
    ExpectColorNear(*withAlpha, EditorColorUVE{0.0F, 224.0F / 255.0F, 117.0F / 255.0F, 128.0F / 255.0F});
}

TEST(EditorColorUVETest, ParseColorHex_RefusesAnythingElseWhole) {
    for (const char* text : {"", "#", "#FF", "#FFFF", "#FFFFF", "#FFFFFFF", "#FFFFFFFFF", "#GG0000", "#FF 000",
                             "##FF0000", "FF0000#", "0xFF0000"}) {
        EXPECT_FALSE(ParseColorHexUVE(text).has_value()) << "'" << text << "'";
    }
}

TEST(EditorColorUVETest, FormatColorHex_RoundsClampsAndRoundTrips) {
    EXPECT_EQ(FormatColorHexUVE(EditorColorUVE{1.0F, 0.5F, 0.0F, 1.0F}, false), "#FF8000");
    EXPECT_EQ(FormatColorHexUVE(EditorColorUVE{1.0F, 0.5F, 0.0F, 0.25F}, true), "#FF800040");
    // Out of range and not-a-number channels are clamped, never printed as garbage.
    EXPECT_EQ(FormatColorHexUVE(EditorColorUVE{2.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(), 1.0F}, false),
              "#FF0000");

    // Every 8-bit value survives format -> parse -> format unchanged.
    for (int byte = 0; byte <= 255; ++byte) {
        const float channel = static_cast<float>(byte) / 255.0F;
        const EditorColorUVE color{channel, 1.0F - channel, channel * 0.5F, channel};
        const std::string hex = FormatColorHexUVE(color, true);
        const std::optional<EditorColorUVE> parsed = ParseColorHexUVE(hex);
        ASSERT_TRUE(parsed.has_value()) << hex;
        EXPECT_EQ(FormatColorHexUVE(*parsed, true), hex);
    }
}

TEST(EditorColorUVETest, RecentColors_NewestFirstNoDuplicatesCapped) {
    std::vector<EditorColorUVE> recents;
    const EditorColorUVE red{1.0F, 0.0F, 0.0F, 1.0F};
    const EditorColorUVE green{0.0F, 1.0F, 0.0F, 1.0F};
    PushRecentColorUVE(recents, red);
    PushRecentColorUVE(recents, green);
    // Picking red again moves it to the front instead of listing it twice.
    PushRecentColorUVE(recents, EditorColorUVE{1.0F, 0.0F, 0.0F, 1.0F});
    ASSERT_EQ(recents.size(), 2U);
    EXPECT_EQ(recents[0], red);
    EXPECT_EQ(recents[1], green);

    for (int index = 0; index < 20; ++index) {
        PushRecentColorUVE(recents, EditorColorUVE{static_cast<float>(index) / 20.0F, 0.5F, 0.5F, 1.0F});
    }
    ASSERT_EQ(recents.size(), kMaxRecentColorsUVE);
    // The newest is first and the oldest ones fell off the end.
    EXPECT_EQ(FormatColorHexUVE(recents.front(), true), FormatColorHexUVE(EditorColorUVE{19.0F / 20.0F, 0.5F, 0.5F, 1.0F}, true));
}

TEST(EditorColorUVETest, SavedColors_RefuseDuplicatesAndStopWhenFull) {
    std::vector<EditorColorUVE> saved;
    EXPECT_TRUE(AddSavedColorUVE(saved, EditorColorUVE{0.2F, 0.4F, 0.6F, 1.0F}));
    EXPECT_FALSE(AddSavedColorUVE(saved, EditorColorUVE{0.2F, 0.4F, 0.6F, 1.0F}));
    ASSERT_EQ(saved.size(), 1U);
    for (std::size_t index = 1; index < kMaxSavedColorsUVE; ++index) {
        EXPECT_TRUE(AddSavedColorUVE(saved, EditorColorUVE{static_cast<float>(index) / 64.0F, 0.0F, 0.0F, 1.0F}));
    }
    ASSERT_EQ(saved.size(), kMaxSavedColorsUVE);
    // A full palette takes no more, and keeps its order.
    EXPECT_FALSE(AddSavedColorUVE(saved, EditorColorUVE{0.9F, 0.9F, 0.9F, 1.0F}));
    EXPECT_EQ(saved.size(), kMaxSavedColorsUVE);
    ExpectColorNear(saved.front(), EditorColorUVE{0.2F, 0.4F, 0.6F, 1.0F});
}

TEST(EditorColorUVETest, HsvConversion_RoundTripsAndKeepsHueThroughGreyAndBlack) {
    const std::vector<EditorColorUVE> colors{
        {1.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F, 1.0F},  {1.0F, 1.0F, 0.0F, 1.0F},
        {0.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 1.0F, 1.0F}, {0.0F, 0.745F, 0.178F, 0.5F}, {0.3F, 0.2F, 0.9F, 1.0F},
    };
    for (const EditorColorUVE& color : colors) {
        const EditorHsvUVE hsv = RgbToHsvUVE(color, EditorHsvUVE{});
        ExpectColorNear(HsvToRgbUVE(hsv, color.a), color, 1e-5F);
    }
    // Pure green is a third of the way round.
    EXPECT_NEAR(RgbToHsvUVE(EditorColorUVE{0.0F, 1.0F, 0.0F, 1.0F}, EditorHsvUVE{}).h, 1.0F / 3.0F, 1e-6F);

    // A grey has no hue and black no saturation: both are carried over from before, so the
    // picker's marker stays where the author left it.
    const EditorHsvUVE previous{0.6F, 0.8F, 0.5F};
    const EditorHsvUVE grey = RgbToHsvUVE(EditorColorUVE{0.4F, 0.4F, 0.4F, 1.0F}, previous);
    EXPECT_FLOAT_EQ(grey.h, 0.6F);
    EXPECT_FLOAT_EQ(grey.s, 0.0F);
    EXPECT_FLOAT_EQ(grey.v, 0.4F);
    const EditorHsvUVE black = RgbToHsvUVE(EditorColorUVE{0.0F, 0.0F, 0.0F, 1.0F}, previous);
    EXPECT_FLOAT_EQ(black.h, 0.6F);
    EXPECT_FLOAT_EQ(black.s, 0.8F);
    EXPECT_FLOAT_EQ(black.v, 0.0F);

    // A hue of exactly 1 is the same red as 0.
    ExpectColorNear(HsvToRgbUVE(EditorHsvUVE{1.0F, 1.0F, 1.0F}, 1.0F), EditorColorUVE{1.0F, 0.0F, 0.0F, 1.0F});
}

} // namespace
} // namespace UVE::Editor::Tests
