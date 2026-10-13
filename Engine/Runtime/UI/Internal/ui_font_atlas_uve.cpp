// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_font_atlas_uve.h"

#include <array>
#include <cstdint>
#include <vector>

// Declarations only - the implementation is generated once, in stb_truetype_impl.cpp, compiled
// with relaxed warnings (this repo's strict -Werror set flags plenty of legitimate vendored-code
// patterns in the implementation body; the declaration section below has none of that).
#include "stb_truetype.h"

#include "uve/ui/ui_embedded_font_uve.h"

namespace UVE::UI {

namespace {

/// Bakes one glyph's shape at the origin (pen at 0,0) so the resulting offsets/UVs are pure,
/// size-independent shape data - AppendTextQuadsUVE() scales and translates them per call.
[[nodiscard]] UIGlyphUVE MakeGlyphUVE(const stbtt_bakedchar& baked) {
    float penX = 0.0F;
    float penY = 0.0F;
    stbtt_aligned_quad quad{};
    stbtt_GetBakedQuad(&baked, UIFontAtlasUVE::kAtlasWidthUVE, UIFontAtlasUVE::kAtlasHeightUVE, 0, &penX, &penY, &quad,
                        1);
    UIGlyphUVE glyph{};
    glyph.offsetX0 = quad.x0;
    glyph.offsetY0 = quad.y0;
    glyph.offsetX1 = quad.x1;
    glyph.offsetY1 = quad.y1;
    glyph.u0 = quad.s0;
    glyph.v0 = quad.t0;
    glyph.u1 = quad.s1;
    glyph.v1 = quad.t1;
    glyph.advanceX = baked.xadvance;
    return glyph;
}

} // namespace

UIFontAtlasUVE::UIFontAtlasUVE() : UIFontAtlasUVE(GetEmbeddedUIFontBytesUVE()) {
}

UIFontAtlasUVE::UIFontAtlasUVE(const std::span<const std::uint8_t> fontBytes) {
    if (fontBytes.empty()) {
        m_valid = false;
        return;
    }
    // stbtt_BakeFontBitmap only ever writes a single-channel (alpha) bitmap; this engine's
    // TextureFormatUVE has no single-channel option (RGBA8Unorm/RGBA16Float/Depth32Float only), so
    // the baked coverage is expanded into RGBA8 (opaque white, coverage in alpha) once here rather
    // than at every GPU-upload call site.
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(kAtlasWidthUVE) * static_cast<std::size_t>(kAtlasHeightUVE),
                                        0U);

    std::array<stbtt_bakedchar, kCharCountUVE> bakedChars{};
    const int bakeResult =
        stbtt_BakeFontBitmap(fontBytes.data(), 0, kBakedFontPixelHeightUVE, coverage.data(),
                              kAtlasWidthUVE, kAtlasHeightUVE, kFirstCharUVE, kCharCountUVE, bakedChars.data());
    if (bakeResult <= 0) {
        m_valid = false;
        return;
    }

    m_bitmap.resize(coverage.size() * 4U);
    for (std::size_t texel = 0U; texel < coverage.size(); ++texel) {
        m_bitmap[texel * 4U + 0U] = 0xFFU;
        m_bitmap[texel * 4U + 1U] = 0xFFU;
        m_bitmap[texel * 4U + 2U] = 0xFFU;
        m_bitmap[texel * 4U + 3U] = coverage[texel];
    }

    for (int index = 0; index < kCharCountUVE; ++index) {
        // stbtt_bakedchar entries with a zero glyph box (e.g. space) still produce a valid,
        // zero-area quad - MakeGlyphUVE handles that naturally, no special-casing needed.
        m_glyphs[static_cast<std::size_t>(index)] = MakeGlyphUVE(bakedChars[static_cast<std::size_t>(index)]);
    }
    m_valid = true;
}

const UIGlyphUVE* UIFontAtlasUVE::FindGlyphUVE(const char character) const noexcept {
    const int code = static_cast<unsigned char>(character);
    if (code < kFirstCharUVE || code >= kFirstCharUVE + kCharCountUVE) {
        return nullptr;
    }
    return &m_glyphs[static_cast<std::size_t>(code - kFirstCharUVE)];
}

void UIFontAtlasUVE::AppendTextQuadsUVE(const std::string_view text, float& cursorX, float& cursorY,
                                        const float fontSizePixels, std::vector<UIGlyphQuadUVE>& outQuads) const {
    if (!m_valid || fontSizePixels <= 0.0F) {
        return;
    }
    const float scale = fontSizePixels / kBakedFontPixelHeightUVE;
    for (const char character : text) {
        const UIGlyphUVE* glyph = FindGlyphUVE(character);
        if (glyph == nullptr) {
            continue;
        }
        UIGlyphQuadUVE quad{};
        quad.x0 = cursorX + glyph->offsetX0 * scale;
        quad.y0 = cursorY + glyph->offsetY0 * scale;
        quad.x1 = cursorX + glyph->offsetX1 * scale;
        quad.y1 = cursorY + glyph->offsetY1 * scale;
        quad.u0 = glyph->u0;
        quad.v0 = glyph->v0;
        quad.u1 = glyph->u1;
        quad.v1 = glyph->v1;
        outQuads.push_back(quad);
        cursorX += glyph->advanceX * scale;
    }
}

float UIFontAtlasUVE::MeasureTextWidthUVE(const std::string_view text,
                                          const float fontSizePixels) const noexcept {
    if (!m_valid || fontSizePixels <= 0.0F) {
        return 0.0F;
    }
    const float scale = fontSizePixels / kBakedFontPixelHeightUVE;
    float width = 0.0F;
    for (const char character : text) {
        const UIGlyphUVE* glyph = FindGlyphUVE(character);
        if (glyph == nullptr) {
            continue;
        }
        width += glyph->advanceX * scale;
    }
    return width;
}

} // namespace UVE::UI
