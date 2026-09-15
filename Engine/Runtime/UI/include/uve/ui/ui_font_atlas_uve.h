// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "uve/ui/ui_draw_batch_uve.h"

namespace UVE::UI {

/// One baked glyph's shape, precomputed at `kBakedFontPixelHeightUVE` with the pen at the origin -
/// `AppendTextQuadsUVE()` scales and translates these by the caller's actual font size and cursor.
/// Kept as our own plain struct (not stb_truetype's `stbtt_bakedchar`) so no vendored type ever
/// appears in a public header.
struct UIGlyphUVE final {
    float offsetX0 = 0.0F;
    float offsetY0 = 0.0F;
    float offsetX1 = 0.0F;
    float offsetY1 = 0.0F;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
    float advanceX = 0.0F;
};

/// A quad for exactly one rendered character, in the same raw-pixel space as UIQuadUVE, already
/// scaled to the caller's requested font size.
struct UIGlyphQuadUVE final {
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
};

/// Bakes the engine's embedded Liberation Sans subset into a fixed single-channel ASCII glyph
/// bitmap once at construction (via vendored `stb_truetype.h`), then serves per-character quads at
/// arbitrary requested font sizes by uniformly scaling the one baked reference size - a real,
/// working bitmap-font technique, not a per-size rebake, so very large/small sizes relative to the
/// baked reference will read softer than a native rasterization would. Only the baked subset's
/// covered characters (printable ASCII, 0x20-0x7E) render; anything else is silently skipped.
/// Thread-safety: not thread-safe; owned and ticked from the scene/runtime thread.
class UIFontAtlasUVE final {
public:
    static constexpr float kBakedFontPixelHeightUVE = 48.0F;
    static constexpr int kAtlasWidthUVE = 512;
    static constexpr int kAtlasHeightUVE = 512;
    static constexpr int kFirstCharUVE = 0x20;
    static constexpr int kCharCountUVE = 0x7F - 0x20;

    UIFontAtlasUVE();

    [[nodiscard]] bool IsValidUVE() const noexcept { return m_valid; }

    /// The baked bitmap, expanded to RGBA8 (opaque white, glyph coverage carried in alpha - this
    /// engine's TextureFormatUVE has no single-channel option), `kAtlasWidthUVE * kAtlasHeightUVE *
    /// 4` bytes, row-major - the GPU upload source for the UI overlay pass. Empty when
    /// `!IsValidUVE()`.
    [[nodiscard]] const std::vector<std::uint8_t>& GetBitmapUVE() const noexcept { return m_bitmap; }

    /// Returns the baked glyph for `character`, or nullptr if it falls outside the covered range.
    [[nodiscard]] const UIGlyphUVE* FindGlyphUVE(char character) const noexcept;

    /// Appends one UIGlyphQuadUVE per renderable character in `text` (unrenderable characters are
    /// skipped but still silently ignored for advance purposes - no tofu/placeholder glyph exists),
    /// starting at the pen position (`cursorX`, `cursorY` - baseline-relative, matching
    /// `stbtt_GetBakedQuad`'s own convention) and advancing it in place, scaled so the rendered
    /// text stands `fontSizePixels` tall regardless of the baked reference size.
    void AppendTextQuadsUVE(std::string_view text, float& cursorX, float& cursorY, float fontSizePixels,
                             std::vector<UIGlyphQuadUVE>& outQuads) const;

private:
    bool m_valid = false;
    std::vector<std::uint8_t> m_bitmap;
    std::array<UIGlyphUVE, kCharCountUVE> m_glyphs{};
};

} // namespace UVE::UI
