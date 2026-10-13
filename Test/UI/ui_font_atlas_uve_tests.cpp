// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_font_atlas_uve.h"

#include "uve/ui/ui_embedded_font_uve.h"

#include <algorithm>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::UI::Tests {
namespace {

TEST(UIFontAtlasUVETest, ConstructionBakesAValidNonEmptyBitmap) {
    const UIFontAtlasUVE atlas;
    ASSERT_TRUE(atlas.IsValidUVE());
    const std::vector<std::uint8_t>& bitmap = atlas.GetBitmapUVE();
    EXPECT_EQ(bitmap.size(),
              static_cast<std::size_t>(UIFontAtlasUVE::kAtlasWidthUVE) *
                  static_cast<std::size_t>(UIFontAtlasUVE::kAtlasHeightUVE) * 4U);
    const bool hasNonZeroAlphaTexel = [&bitmap] {
        for (std::size_t index = 3U; index < bitmap.size(); index += 4U) {
            if (bitmap[index] != 0U) {
                return true;
            }
        }
        return false;
    }();
    EXPECT_TRUE(hasNonZeroAlphaTexel);
}

TEST(UIFontAtlasUVETest, FindGlyphUVE_CoversPrintableAsciiAndRejectsOutOfRange) {
    const UIFontAtlasUVE atlas;
    ASSERT_TRUE(atlas.IsValidUVE());

    EXPECT_NE(atlas.FindGlyphUVE('A'), nullptr);
    EXPECT_NE(atlas.FindGlyphUVE('z'), nullptr);
    EXPECT_NE(atlas.FindGlyphUVE('0'), nullptr);
    EXPECT_NE(atlas.FindGlyphUVE(' '), nullptr);
    EXPECT_NE(atlas.FindGlyphUVE('~'), nullptr);

    EXPECT_EQ(atlas.FindGlyphUVE(static_cast<char>(0x01)), nullptr);
    EXPECT_EQ(atlas.FindGlyphUVE(static_cast<char>(0x7F)), nullptr);

    const UIGlyphUVE* glyphA = atlas.FindGlyphUVE('A');
    ASSERT_NE(glyphA, nullptr);
    EXPECT_GT(glyphA->advanceX, 0.0F);
    EXPECT_GE(glyphA->u0, 0.0F);
    EXPECT_LE(glyphA->u1, 1.0F);
    EXPECT_GE(glyphA->v0, 0.0F);
    EXPECT_LE(glyphA->v1, 1.0F);
}

TEST(UIFontAtlasUVETest, AppendTextQuadsUVE_AdvancesCursorAndScalesWithFontSize) {
    const UIFontAtlasUVE atlas;
    ASSERT_TRUE(atlas.IsValidUVE());

    float cursorX = 0.0F;
    float cursorY = 0.0F;
    std::vector<UIGlyphQuadUVE> quadsAtReferenceSize;
    atlas.AppendTextQuadsUVE("Hi", cursorX, cursorY, UIFontAtlasUVE::kBakedFontPixelHeightUVE, quadsAtReferenceSize);
    ASSERT_EQ(quadsAtReferenceSize.size(), 2U);
    EXPECT_GT(cursorX, 0.0F);
    EXPECT_LT(quadsAtReferenceSize[0].x0, quadsAtReferenceSize[1].x0)
        << "second character should be placed to the right of the first";

    float doubleCursorX = 0.0F;
    float doubleCursorY = 0.0F;
    std::vector<UIGlyphQuadUVE> quadsAtDoubleSize;
    atlas.AppendTextQuadsUVE("Hi", doubleCursorX, doubleCursorY, UIFontAtlasUVE::kBakedFontPixelHeightUVE * 2.0F,
                             quadsAtDoubleSize);
    EXPECT_NEAR(doubleCursorX, cursorX * 2.0F, 0.01F) << "advance should scale linearly with font size";
}

TEST(UIFontAtlasUVETest, AppendTextQuadsUVE_SkipsUnrenderableCharactersWithoutCrashing) {
    const UIFontAtlasUVE atlas;
    ASSERT_TRUE(atlas.IsValidUVE());

    float cursorX = 0.0F;
    float cursorY = 0.0F;
    std::vector<UIGlyphQuadUVE> quads;
    const std::string textWithControlCharacter = std::string("A") + '\x01' + "B";
    atlas.AppendTextQuadsUVE(textWithControlCharacter, cursorX, cursorY, UIFontAtlasUVE::kBakedFontPixelHeightUVE,
                             quads);
    EXPECT_EQ(quads.size(), 2U);
}

TEST(UIFontAtlasUVETest, BytesConstructor_BakesTheEmbeddedFontIdenticallyToTheDefault) {
    // The asset path (bake from caller bytes) fed the embedded bytes must reproduce the default
    // atlas bit-for-bit: same bitmap, same glyph quads and advances.
    const UIFontAtlasUVE def;
    ASSERT_TRUE(def.IsValidUVE());
    const UIFontAtlasUVE fromBytes{GetEmbeddedUIFontBytesUVE()};
    ASSERT_TRUE(fromBytes.IsValidUVE());
    EXPECT_EQ(fromBytes.GetBitmapUVE(), def.GetBitmapUVE());
    for (char c = ' '; c <= '~'; ++c) {
        const UIGlyphUVE* expected = def.FindGlyphUVE(c);
        const UIGlyphUVE* actual = fromBytes.FindGlyphUVE(c);
        ASSERT_NE(expected, nullptr);
        ASSERT_NE(actual, nullptr);
        EXPECT_FLOAT_EQ(actual->advanceX, expected->advanceX);
        EXPECT_FLOAT_EQ(actual->offsetX0, expected->offsetX0);
        EXPECT_FLOAT_EQ(actual->u1, expected->u1);
    }
}

TEST(UIFontAtlasUVETest, BytesConstructor_UnparseableBytesFailClosed) {
    // All zeros, not a short buffer: stb_truetype reads the table directory without bounds
    // checks, so the failing input must be big enough to read safely but parse to nothing.
    const std::vector<std::uint8_t> garbage(1024U, 0x00U);
    const UIFontAtlasUVE bad{std::span<const std::uint8_t>{garbage}};
    EXPECT_FALSE(bad.IsValidUVE());
    EXPECT_TRUE(bad.GetBitmapUVE().empty());
    const UIFontAtlasUVE empty{std::span<const std::uint8_t>{}};
    EXPECT_FALSE(empty.IsValidUVE());
}

} // namespace
} // namespace UVE::UI::Tests
