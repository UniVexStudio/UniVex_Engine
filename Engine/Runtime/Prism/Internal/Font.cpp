// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#if defined(PRISM_HAS_HARFBUZZ)
#include <hb-ft.h>
#endif

namespace prism {
namespace {
FT_Library g_lib = nullptr;
int g_refs = 0;
} // namespace

Font::Font(const std::string& path, int px) : m_px(px) {
  if (!g_lib && FT_Init_FreeType(&g_lib)) return;
  g_refs++;
  FT_Face face = nullptr;
  if (FT_New_Face(g_lib, path.c_str(), 0, &face)) return;
  m_face = face;
  if (!FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0) {
    // bitmap-only colour fonts (CBDT emoji) have fixed strikes: pick the closest, scale glyphs down at load time
    int best = 0;
    for (int i = 1; i < face->num_fixed_sizes; i++)
      if (std::abs(face->available_sizes[i].height - px) < std::abs(face->available_sizes[best].height - px)) best = i;
    FT_Select_Size(face, best);
    m_bitmapScale = float(px) / float(std::max<FT_Short>(1, face->available_sizes[best].height));
  } else FT_Set_Pixel_Sizes(face, 0, px);
  const auto& m = face->size->metrics;
  m_ascent = std::ceil(m.ascender / 64.f * m_bitmapScale);
  m_descent = std::ceil(-m.descender / 64.f * m_bitmapScale);
  if (FT_Load_Char(face, 'H', FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP) == 0)
    m_cap = std::round(face->glyph->metrics.horiBearingY / 64.f);
  else
    m_cap = std::round(px * 0.72f);
}

Font::~Font() {
#if defined(PRISM_HAS_HARFBUZZ)
  if (m_hb) hb_font_destroy(static_cast<hb_font_t*>(m_hb));   // before the FreeType face it references
#endif
  if (m_face) FT_Done_Face(m_face);
  if (g_refs > 0 && --g_refs == 0 && g_lib) {
    FT_Done_FreeType(g_lib);
    g_lib = nullptr;
  }
}

bool Font::has(char32_t cp) const { return m_face && FT_Get_Char_Index(m_face, cp) != 0; }
bool Font::hasColor() const { return m_face && FT_HAS_COLOR(m_face); }
void* Font::shaper() const {
#if defined(PRISM_HAS_HARFBUZZ)
  if (!m_hb && m_face) m_hb = hb_ft_font_create_referenced(m_face);
  return m_hb;
#else
  return nullptr;
#endif
}

const Glyph& Font::glyph(char32_t cp) const {
  auto it = m_cache.find(cp);
  if (it != m_cache.end()) return it->second;
  Glyph g;
  if (m_face) {
    FT_UInt idx = (cp & kGlyphIndexKey) ? FT_UInt(cp & ~kGlyphIndexKey) : FT_Get_Char_Index(m_face, cp);
    FT_Int32 flags = FT_LOAD_TARGET_LIGHT | FT_LOAD_RENDER | (FT_HAS_COLOR(m_face) ? FT_LOAD_COLOR : 0);
    if (FT_Load_Glyph(m_face, idx, flags) == 0) {
      FT_GlyphSlot s = m_face->glyph;
      const float k = m_bitmapScale;
      int sw = int(s->bitmap.width), sh = int(s->bitmap.rows);
      g.w = std::max(0, int(std::lround(sw * k)));
      g.h = std::max(0, int(std::lround(sh * k)));
      g.left = int(std::lround(s->bitmap_left * k));
      g.top = int(std::lround(s->bitmap_top * k));
      g.advance = float(s->linearHoriAdvance) / 65536.f;
      if (g.advance <= 0 || !FT_IS_SCALABLE(m_face)) g.advance = float(s->advance.x) / 64.f;
      g.advance *= (FT_IS_SCALABLE(m_face) ? 1.f : k);
      g.bitmap.resize(size_t(g.w) * size_t(g.h));
      const bool bgra = s->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA;
      if (bgra) g.argb.resize(g.bitmap.size());
      // box-filter down-sample when scaling (k < 1), identity copy otherwise
      for (int y = 0; y < g.h; y++)
        for (int x = 0; x < g.w; x++) {
          int x0 = int(x / k), x1 = std::max(x0 + 1, int((x + 1) / k)), y0 = int(y / k), y1 = std::max(y0 + 1, int((y + 1) / k));
          x1 = std::min(x1, sw); y1 = std::min(y1, sh);
          unsigned a = 0, r = 0, gg = 0, b = 0, n = 0;
          for (int yy = y0; yy < y1; yy++)
            for (int xx = x0; xx < x1; xx++) {
              const unsigned char* px = s->bitmap.buffer + yy * s->bitmap.pitch;
              if (bgra) { b += px[xx * 4]; gg += px[xx * 4 + 1]; r += px[xx * 4 + 2]; a += px[xx * 4 + 3]; }
              else if (s->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) a += (px[xx >> 3] & (0x80 >> (xx & 7))) ? 255 : 0;
              else a += px[xx];
              n++;
            }
          n = std::max(1u, n);
          g.bitmap[size_t(y) * g.w + x] = uint8_t(a / n);
          if (bgra) {
            unsigned A = a / n;    // FreeType BGRA is premultiplied: un-premultiply into straight alpha
            auto un = [&](unsigned c) { return A ? std::min(255u, (c / n) * 255u / A) : 0u; };
            g.argb[size_t(y) * g.w + x] = (A << 24) | (un(r) << 16) | (un(gg) << 8) | un(b);
          }
        }
    }
  }
  return m_cache.emplace(cp, std::move(g)).first->second;
}

float Font::width(std::string_view s) const {
  float w = 0;
  for (size_t i = 0; i < s.size();) w += glyph(decodeUtf8(s, i)).advance;
  return w;
}

float Font::widthTo(std::string_view s, size_t n) const { return width(s.substr(0, std::min(n, s.size()))); }

size_t Font::indexAt(std::string_view s, float x) const {
  float pen = 0;
  for (size_t i = 0; i < s.size();) {
    size_t start = i;
    float adv = glyph(decodeUtf8(s, i)).advance;
    if (x < pen + adv * 0.5f) return start;
    pen += adv;
  }
  return s.size();
}

std::string Font::ellipsize(std::string_view s, float maxW) const {
  if (width(s) <= maxW) return std::string(s);
  const std::string dots = "\xE2\x80\xA6";
  float limit = maxW - width(dots);
  if (limit <= 0) return dots;
  float pen = 0;
  size_t cut = 0;
  for (size_t i = 0; i < s.size();) {
    size_t start = i;
    float adv = glyph(decodeUtf8(s, i)).advance;
    if (pen + adv > limit) break;
    pen += adv;
    cut = i;
    (void)start;
  }
  return std::string(s.substr(0, cut)) + dots;
}

} // namespace prism
