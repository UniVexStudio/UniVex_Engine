// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_font_atlas_uve.h"

#include <algorithm>
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

} // namespace
} // namespace UVE::UI::Tests
