// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - software 2D rasterizer. All UI (and the demo viewport) is drawn through this.
#pragma once
#include "Core.h"
#include "Brush.h"
#include "Font.h"
#include <span>

namespace prism {

class Canvas {
public:
  virtual ~Canvas() = default;
  virtual void resize(int w, int h);
  // O(1) exchange of the pixel buffer, size, clip and clip stack (multi-window state switching).
  void swapContents(Canvas& o) noexcept {
    std::swap(m_w, o.m_w); std::swap(m_h, o.m_h); m_px.swap(o.m_px);
    std::swap(m_cx0, o.m_cx0); std::swap(m_cy0, o.m_cy0); std::swap(m_cx1, o.m_cx1); std::swap(m_cy1, o.m_cy1);
    m_stack.swap(o.m_stack);
  }
  int width() const { return m_w; }
  int height() const { return m_h; }
  uint32_t* pixels() { return m_px.data(); }
  const uint32_t* pixels() const { return m_px.data(); }
  virtual void clear(Color c);

  // Clipping (integer pixel rect, intersected with the current clip)
  virtual void pushClip(Rect r);
  virtual void popClip();
  Rect clip() const { return {float(m_cx0), float(m_cy0), float(m_cx1 - m_cx0), float(m_cy1 - m_cy0)}; }

  // Shapes. Rectangles are snapped to the pixel grid; curves and diagonals are anti-aliased.
  virtual void fillRect(Rect r, Color c);
  void strokeRect(Rect r, Color c, int thickness = 1);
  virtual void fillRoundRect(Rect r, float radius, Color c);
  virtual void strokeRoundRect(Rect r, float radius, Color c, int thickness = 1);
  virtual void fillCircle(Vec2 center, float radius, Color c);
  virtual void strokeCircle(Vec2 center, float radius, Color c, float thickness = 1);
  virtual void line(Vec2 a, Vec2 b, Color c, float thickness = 1);
  void polyline(std::span<const Vec2> pts, Color c, float thickness = 1, bool closed = false);
  void hline(float x0, float x1, float y, Color c) { fillRect({x0, y, x1 - x0, 1}, c); }
  void vline(float x, float y0, float y1, Color c) { fillRect({x, y0, 1, y1 - y0}, c); }
  virtual void fillPolygon(std::span<const Vec2> pts, Color c, bool aa = true); // convex only
  virtual void gradientV(Rect r, Color top, Color bottom);
  virtual void gradientH(Rect r, Color left, Color right);
  virtual void nineSlice(const uint32_t* src, int sw, int sh, Margin margins, Rect dst);
  virtual void drawBrush(Rect r, const Brush& brush);
  void shadow(Rect r, float radius, int spread, Color c);

  // Text. `p` is the top-left of the line box (ascent above the baseline).
  virtual void text(const Font& f, Vec2 p, std::string_view s, Color c);
  // Positioned glyphs (shaped text). key = code point, or kGlyphIndexKey | glyph index. (x, y) are offsets from p
  // along the baseline (y up is negative); p is the top-left of the line box, like text().
  struct GlyphPos { char32_t key; float x, y; };
  virtual void glyphRun(const Font& f, Vec2 p, std::span<const GlyphPos> glyphs, Color c);
  // Text vertically centred (by cap height) inside r.
  void textIn(const Font& f, Rect r, std::string_view s, Color c, HAlign h = HAlign::Left, bool ellipsis = true);

  virtual void blit(const uint32_t* src, int sw, int sh, Vec2 dst);
  // Nearest-neighbour scaled copy of an ARGB image (row stride in pixels) into dst, honouring the clip.
  virtual void blitScaled(const uint32_t* src, int sw, int sh, int stride, Rect dst);
  // Like blitScaled, but composites the straight-alpha source over what is there (icons, cut-out thumbnails).
  virtual void blitBlend(const uint32_t* src, int sw, int sh, int stride, Rect dst);
  // Writes `argb` directly (no blending). With alpha 0 this punches a transparent hole for host-drawn content.
  virtual void fillRaw(Rect r, uint32_t argb);
  bool savePng(const std::string& path) const;
  static bool writePng(const std::string& path, const uint32_t* argb, int w, int h, int stride);

protected:
  struct IR { int x0, y0, x1, y1; };
  int m_w = 0, m_h = 0;
  std::vector<uint32_t> m_px;
  int m_cx0 = 0, m_cy0 = 0, m_cx1 = 0, m_cy1 = 0;
  std::vector<IR> m_stack;
  void plot(int x, int y, Color c, int cov);
  void drawGlyph(const Glyph& g, int gx, int base, Color col);
};

} // namespace prism
