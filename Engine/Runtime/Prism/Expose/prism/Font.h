// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - FreeType-backed bitmap font with a lazy glyph cache.
#pragma once
#include "Core.h"
#include <unordered_map>

struct FT_FaceRec_;

namespace prism {

struct Glyph {
  int w = 0, h = 0, left = 0, top = 0;
  float advance = 0;
  std::vector<uint8_t> bitmap; // 8-bit coverage
  std::vector<uint32_t> argb;   // colour glyphs (emoji): straight-alpha 0xAARRGGBB, same w x h; bitmap holds alpha
};

// Font::glyph() key for a glyph *index* (shaped text) instead of a code point.
constexpr char32_t kGlyphIndexKey = 0x80000000u;

class Font {
public:
  Font(const std::string& path, int pixelSize);
  ~Font();
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  bool ok() const { return m_face != nullptr; }
  int pixelSize() const { return m_px; }
  float ascent() const { return m_ascent; }
  float descent() const { return m_descent; }
  float lineHeight() const { return m_ascent + m_descent; }
  float capHeight() const { return m_cap; }

  bool has(char32_t cp) const;
  FT_FaceRec_* face() const { return m_face; }
  float bitmapScale() const { return m_bitmapScale; }     // < 1 for fixed-size colour fonts scaled to pixelSize
  bool hasColor() const;
  void* shaper() const;          // hb_font_t* owned by this font (null without HarfBuzz)                       // face maps this code point (not .notdef)
  const Glyph& glyph(char32_t cp) const;
  float advance(char32_t cp) const { return glyph(cp).advance; }
  float width(std::string_view s) const;
  float widthTo(std::string_view s, size_t byteIndex) const;
  size_t indexAt(std::string_view s, float x) const; // nearest caret boundary (byte index)
  std::string ellipsize(std::string_view s, float maxWidth) const;

private:
  FT_FaceRec_* m_face = nullptr;
  int m_px = 0;
  float m_ascent = 0, m_descent = 0, m_cap = 0, m_bitmapScale = 1;
  mutable std::unordered_map<char32_t, Glyph> m_cache;
  mutable void* m_hb = nullptr;
};

} // namespace prism
