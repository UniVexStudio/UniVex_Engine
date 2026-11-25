// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_font_atlas.h"

#include "ui_embedded_font.h"

#include <algorithm>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::UI::Tests {
namespace {

TEST(UIFontAtlasTest, ConstructionBakesAValidNonEmptyBitmap) {
    const UIFontAtlas atlas;
    ASSERT_TRUE(atlas.IsValid());
    const std::vector<std::uint8_t>& bitmap = atlas.GetBitmap();
    EXPECT_EQ(bitmap.size(),
              static_cast<std::size_t>(UIFontAtlas::kAtlasWidth) *
                  static_cast<std::size_t>(UIFontAtlas::kAtlasHeight) * 4U);
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

TEST(UIFontAtlasTest, FindGlyph_CoversPrintableAsciiAndRejectsOutOfRange) {
    const UIFontAtlas atlas;
    ASSERT_TRUE(atlas.IsValid());

    EXPECT_NE(atlas.FindGlyph('A'), nullptr);
    EXPECT_NE(atlas.FindGlyph('z'), nullptr);
    EXPECT_NE(atlas.FindGlyph('0'), nullptr);
    EXPECT_NE(atlas.FindGlyph(' '), nullptr);
    EXPECT_NE(atlas.FindGlyph('~'), nullptr);

    EXPECT_EQ(atlas.FindGlyph(static_cast<char>(0x01)), nullptr);
    EXPECT_EQ(atlas.FindGlyph(static_cast<char>(0x7F)), nullptr);

    const UIGlyph* glyphA = atlas.FindGlyph('A');
    ASSERT_NE(glyphA, nullptr);
    EXPECT_GT(glyphA->advanceX, 0.0F);
    EXPECT_GE(glyphA->u0, 0.0F);
    EXPECT_LE(glyphA->u1, 1.0F);
    EXPECT_GE(glyphA->v0, 0.0F);
    EXPECT_LE(glyphA->v1, 1.0F);
}

TEST(UIFontAtlasTest, AppendTextQuads_AdvancesCursorAndScalesWithFontSize) {
    const UIFontAtlas atlas;
    ASSERT_TRUE(atlas.IsValid());

    float cursorX = 0.0F;
    float cursorY = 0.0F;
    std::vector<UIGlyphQuad> quadsAtReferenceSize;
    atlas.AppendTextQuads("Hi", cursorX, cursorY, UIFontAtlas::kBakedFontPixelHeight, quadsAtReferenceSize);
    ASSERT_EQ(quadsAtReferenceSize.size(), 2U);
    EXPECT_GT(cursorX, 0.0F);
    EXPECT_LT(quadsAtReferenceSize[0].x0, quadsAtReferenceSize[1].x0)
        << "second character should be placed to the right of the first";

    float doubleCursorX = 0.0F;
    float doubleCursorY = 0.0F;
    std::vector<UIGlyphQuad> quadsAtDoubleSize;
    atlas.AppendTextQuads("Hi", doubleCursorX, doubleCursorY, UIFontAtlas::kBakedFontPixelHeight * 2.0F,
                             quadsAtDoubleSize);
    EXPECT_NEAR(doubleCursorX, cursorX * 2.0F, 0.01F) << "advance should scale linearly with font size";
}

TEST(UIFontAtlasTest, AppendTextQuads_SkipsUnrenderableCharactersWithoutCrashing) {
    const UIFontAtlas atlas;
    ASSERT_TRUE(atlas.IsValid());

    float cursorX = 0.0F;
    float cursorY = 0.0F;
    std::vector<UIGlyphQuad> quads;
    const std::string textWithControlCharacter = std::string("A") + '\x01' + "B";
    atlas.AppendTextQuads(textWithControlCharacter, cursorX, cursorY, UIFontAtlas::kBakedFontPixelHeight,
                             quads);
    EXPECT_EQ(quads.size(), 2U);
}

TEST(UIFontAtlasTest, BytesConstructor_BakesTheEmbeddedFontIdenticallyToTheDefault) {
    // The asset path (bake from caller bytes) fed the embedded bytes must reproduce the default
    // atlas bit-for-bit: same bitmap, same glyph quads and advances.
    const UIFontAtlas def;
    ASSERT_TRUE(def.IsValid());
    const UIFontAtlas fromBytes{GetEmbeddedUIFontBytes()};
    ASSERT_TRUE(fromBytes.IsValid());
    EXPECT_EQ(fromBytes.GetBitmap(), def.GetBitmap());
    for (char c = ' '; c <= '~'; ++c) {
        const UIGlyph* expected = def.FindGlyph(c);
        const UIGlyph* actual = fromBytes.FindGlyph(c);
        ASSERT_NE(expected, nullptr);
        ASSERT_NE(actual, nullptr);
        EXPECT_FLOAT_EQ(actual->advanceX, expected->advanceX);
        EXPECT_FLOAT_EQ(actual->offsetX0, expected->offsetX0);
        EXPECT_FLOAT_EQ(actual->u1, expected->u1);
    }
}

TEST(UIFontAtlasTest, BytesConstructor_UnparseableBytesFailClosed) {
    // All zeros, not a short buffer: stb_truetype reads the table directory without bounds
    // checks, so the failing input must be big enough to read safely but parse to nothing.
    const std::vector<std::uint8_t> garbage(1024U, 0x00U);
    const UIFontAtlas bad{std::span<const std::uint8_t>{garbage}};
    EXPECT_FALSE(bad.IsValid());
    EXPECT_TRUE(bad.GetBitmap().empty());
    const UIFontAtlas empty{std::span<const std::uint8_t>{}};
    EXPECT_FALSE(empty.IsValid());
}

} // namespace
} // namespace UVE::UI::Tests
