// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_color.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

void ExpectColorNear(const EditorColor& actual, const EditorColor& expected, const float tolerance = 1e-5F) {
    EXPECT_NEAR(actual.r, expected.r, tolerance);
    EXPECT_NEAR(actual.g, expected.g, tolerance);
    EXPECT_NEAR(actual.b, expected.b, tolerance);
    EXPECT_NEAR(actual.a, expected.a, tolerance);
}

TEST(EditorColorTest, ParseColorHex_AcceptsThreeSixAndEightDigitForms) {
    // Six digits, with and without the marker, either case, spaces around it ignored.
    for (const char* text : {"#FF8000", "FF8000", "ff8000", "  #Ff8000 \t"}) {
        const std::optional<EditorColor> color = ParseColorHex(text);
        ASSERT_TRUE(color.has_value()) << text;
        ExpectColorNear(*color, EditorColor{1.0F, 128.0F / 255.0F, 0.0F, 1.0F});
    }
    // Three digits double each one: F is FF, not F0.
    const std::optional<EditorColor> shorthand = ParseColorHex("#F80");
    ASSERT_TRUE(shorthand.has_value());
    ExpectColorNear(*shorthand, EditorColor{1.0F, 136.0F / 255.0F, 0.0F, 1.0F});
    // Eight digits carry alpha.
    const std::optional<EditorColor> withAlpha = ParseColorHex("#00E07580");
    ASSERT_TRUE(withAlpha.has_value());
    ExpectColorNear(*withAlpha, EditorColor{0.0F, 224.0F / 255.0F, 117.0F / 255.0F, 128.0F / 255.0F});
}

TEST(EditorColorTest, ParseColorHex_RefusesAnythingElseWhole) {
    for (const char* text : {"", "#", "#FF", "#FFFF", "#FFFFF", "#FFFFFFF", "#FFFFFFFFF", "#GG0000", "#FF 000",
                             "##FF0000", "FF0000#", "0xFF0000"}) {
        EXPECT_FALSE(ParseColorHex(text).has_value()) << "'" << text << "'";
    }
}

TEST(EditorColorTest, FormatColorHex_RoundsClampsAndRoundTrips) {
    EXPECT_EQ(FormatColorHex(EditorColor{1.0F, 0.5F, 0.0F, 1.0F}, false), "#FF8000");
    EXPECT_EQ(FormatColorHex(EditorColor{1.0F, 0.5F, 0.0F, 0.25F}, true), "#FF800040");
    // Out of range and not-a-number channels are clamped, never printed as garbage.
    EXPECT_EQ(FormatColorHex(EditorColor{2.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(), 1.0F}, false),
              "#FF0000");

    // Every 8-bit value survives format -> parse -> format unchanged.
    for (int byte = 0; byte <= 255; ++byte) {
        const float channel = static_cast<float>(byte) / 255.0F;
        const EditorColor color{channel, 1.0F - channel, channel * 0.5F, channel};
        const std::string hex = FormatColorHex(color, true);
        const std::optional<EditorColor> parsed = ParseColorHex(hex);
        ASSERT_TRUE(parsed.has_value()) << hex;
        EXPECT_EQ(FormatColorHex(*parsed, true), hex);
    }
}

TEST(EditorColorTest, RecentColors_NewestFirstNoDuplicatesCapped) {
    std::vector<EditorColor> recents;
    const EditorColor red{1.0F, 0.0F, 0.0F, 1.0F};
    const EditorColor green{0.0F, 1.0F, 0.0F, 1.0F};
    PushRecentColor(recents, red);
    PushRecentColor(recents, green);
    // Picking red again moves it to the front instead of listing it twice.
    PushRecentColor(recents, EditorColor{1.0F, 0.0F, 0.0F, 1.0F});
    ASSERT_EQ(recents.size(), 2U);
    EXPECT_EQ(recents[0], red);
    EXPECT_EQ(recents[1], green);

    for (int index = 0; index < 20; ++index) {
        PushRecentColor(recents, EditorColor{static_cast<float>(index) / 20.0F, 0.5F, 0.5F, 1.0F});
    }
    ASSERT_EQ(recents.size(), kMaxRecentColors);
    // The newest is first and the oldest ones fell off the end.
    EXPECT_EQ(FormatColorHex(recents.front(), true), FormatColorHex(EditorColor{19.0F / 20.0F, 0.5F, 0.5F, 1.0F}, true));
}

TEST(EditorColorTest, SavedColors_RefuseDuplicatesAndStopWhenFull) {
    std::vector<EditorColor> saved;
    EXPECT_TRUE(AddSavedColor(saved, EditorColor{0.2F, 0.4F, 0.6F, 1.0F}));
    EXPECT_FALSE(AddSavedColor(saved, EditorColor{0.2F, 0.4F, 0.6F, 1.0F}));
    ASSERT_EQ(saved.size(), 1U);
    for (std::size_t index = 1; index < kMaxSavedColors; ++index) {
        EXPECT_TRUE(AddSavedColor(saved, EditorColor{static_cast<float>(index) / 64.0F, 0.0F, 0.0F, 1.0F}));
    }
    ASSERT_EQ(saved.size(), kMaxSavedColors);
    // A full palette takes no more, and keeps its order.
    EXPECT_FALSE(AddSavedColor(saved, EditorColor{0.9F, 0.9F, 0.9F, 1.0F}));
    EXPECT_EQ(saved.size(), kMaxSavedColors);
    ExpectColorNear(saved.front(), EditorColor{0.2F, 0.4F, 0.6F, 1.0F});
}

TEST(EditorColorTest, HsvConversion_RoundTripsAndKeepsHueThroughGreyAndBlack) {
    const std::vector<EditorColor> colors{
        {1.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F, 1.0F},  {1.0F, 1.0F, 0.0F, 1.0F},
        {0.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 1.0F, 1.0F}, {0.0F, 0.745F, 0.178F, 0.5F}, {0.3F, 0.2F, 0.9F, 1.0F},
    };
    for (const EditorColor& color : colors) {
        const EditorHsv hsv = RgbToHsv(color, EditorHsv{});
        ExpectColorNear(HsvToRgb(hsv, color.a), color, 1e-5F);
    }
    // Pure green is a third of the way round.
    EXPECT_NEAR(RgbToHsv(EditorColor{0.0F, 1.0F, 0.0F, 1.0F}, EditorHsv{}).h, 1.0F / 3.0F, 1e-6F);

    // A grey has no hue and black no saturation: both are carried over from before, so the
    // picker's marker stays where the author left it.
    const EditorHsv previous{0.6F, 0.8F, 0.5F};
    const EditorHsv grey = RgbToHsv(EditorColor{0.4F, 0.4F, 0.4F, 1.0F}, previous);
    EXPECT_FLOAT_EQ(grey.h, 0.6F);
    EXPECT_FLOAT_EQ(grey.s, 0.0F);
    EXPECT_FLOAT_EQ(grey.v, 0.4F);
    const EditorHsv black = RgbToHsv(EditorColor{0.0F, 0.0F, 0.0F, 1.0F}, previous);
    EXPECT_FLOAT_EQ(black.h, 0.6F);
    EXPECT_FLOAT_EQ(black.s, 0.8F);
    EXPECT_FLOAT_EQ(black.v, 0.0F);

    // A hue of exactly 1 is the same red as 0.
    ExpectColorNear(HsvToRgb(EditorHsv{1.0F, 1.0F, 1.0F}, 1.0F), EditorColor{1.0F, 0.0F, 0.0F, 1.0F});
}

TEST(EditorColorTest, LinearSrgbHdrExposureAndChannelNudgeWorkDeterministically) {
    const EditorColor linear{0.214041F, 0.5F, 0.01F, 0.75F};
    const EditorColor srgb = LinearToSrgbColor(linear);
    const EditorColor roundTripped = SrgbToLinearColor(srgb);
    ExpectColorNear(roundTripped, linear, 1e-5F);

    const EditorColor hdr = ApplyHdrExposureToColor(EditorColor{0.5F, 0.25F, 0.1F, 0.8F}, 2.0F, false);
    EXPECT_FLOAT_EQ(hdr.r, 2.0F);
    EXPECT_FLOAT_EQ(hdr.g, 1.0F);
    EXPECT_FLOAT_EQ(hdr.a, 0.8F);

    const EditorColor ldrClamped = ApplyHdrExposureToColor(EditorColor{0.5F, 0.25F, 0.1F, 0.8F}, 2.0F, true);
    EXPECT_FLOAT_EQ(ldrClamped.r, 1.0F);

    EditorColor nudged{0.95F, 0.5F, 0.2F, 1.0F};
    NudgeColorChannel(nudged, 0U, 0.2F, true);
    EXPECT_FLOAT_EQ(nudged.r, 1.0F);
    NudgeColorChannel(nudged, 0U, 0.5F, false);
    EXPECT_FLOAT_EQ(nudged.r, 1.5F);
}

} // namespace
} // namespace UVE::Editor::Tests
