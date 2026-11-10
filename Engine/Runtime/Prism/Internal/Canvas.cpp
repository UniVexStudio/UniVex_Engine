// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Canvas.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace prism {
namespace {

inline uint32_t packRGB(Color c) { return 0xFF000000u | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b; }

inline void blendPx(uint32_t& d, Color c, int a) {
  if (a <= 0) return;
  if (a >= 255) { d = packRGB(c); return; }
  int inv = 255 - a;
  uint32_t r = (((d >> 16) & 255) * inv + c.r * a + 127) / 255;
  uint32_t g = (((d >> 8) & 255) * inv + c.g * a + 127) / 255;
  uint32_t b = ((d & 255) * inv + c.b * a + 127) / 255;
  d = 0xFF000000u | (r << 16) | (g << 8) | b;
}

inline int effAlpha(Color c, int cov) { return c.a == 255 ? cov : (c.a * cov + 127) / 255; }
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline int cov255(float v) { return int(clamp01(v) * 255.f + 0.5f); }

// Light-on-dark text looks thin with linear coverage; a mild curve restores weight.
struct TextLut {
  uint8_t v[256];
  TextLut() {
    for (int i = 0; i < 256; i++) v[i] = uint8_t(std::lround(std::pow(i / 255.0, 0.80) * 255.0));
  }
};
const TextLut g_lut;

} // namespace

void Canvas::resize(int w, int h) {
  m_w = std::max(1, w);
  m_h = std::max(1, h);
  m_px.assign(size_t(m_w) * m_h, 0xFF000000u);
  m_cx0 = m_cy0 = 0;
  m_cx1 = m_w;
  m_cy1 = m_h;
  m_stack.clear();
}

void Canvas::clear(Color c) { std::fill(m_px.begin(), m_px.end(), packRGB(c)); }

void Canvas::pushClip(Rect r) {
  m_stack.push_back({m_cx0, m_cy0, m_cx1, m_cy1});
  int x0 = std::max(m_cx0, int(std::lround(r.x)));
  int y0 = std::max(m_cy0, int(std::lround(r.y)));
  int x1 = std::min(m_cx1, int(std::lround(r.x + r.w)));
  int y1 = std::min(m_cy1, int(std::lround(r.y + r.h)));
  m_cx0 = x0; m_cy0 = y0;
  m_cx1 = std::max(x0, x1);
  m_cy1 = std::max(y0, y1);
}

void Canvas::popClip() {
  if (m_stack.empty()) return;
  IR c = m_stack.back();
  m_stack.pop_back();
  m_cx0 = c.x0; m_cy0 = c.y0; m_cx1 = c.x1; m_cy1 = c.y1;
}

void Canvas::plot(int x, int y, Color c, int cov) {
  if (x < m_cx0 || x >= m_cx1 || y < m_cy0 || y >= m_cy1) return;
  blendPx(m_px[size_t(y) * m_w + x], c, effAlpha(c, cov));
}

void Canvas::fillRect(Rect r, Color c) {
  if (c.a == 0) return;
  int x0 = std::max(m_cx0, int(std::lround(r.x)));
  int y0 = std::max(m_cy0, int(std::lround(r.y)));
  int x1 = std::min(m_cx1, int(std::lround(r.x + r.w)));
  int y1 = std::min(m_cy1, int(std::lround(r.y + r.h)));
  if (x0 >= x1 || y0 >= y1) return;
  if (c.a == 255) {
    uint32_t p = packRGB(c);
    for (int y = y0; y < y1; y++) std::fill_n(&m_px[size_t(y) * m_w + x0], x1 - x0, p);
  } else {
    for (int y = y0; y < y1; y++) {
      uint32_t* row = &m_px[size_t(y) * m_w];
      for (int x = x0; x < x1; x++) blendPx(row[x], c, c.a);
    }
  }
}

void Canvas::strokeRect(Rect r, Color c, int t) {
  int x0 = int(std::lround(r.x)), y0 = int(std::lround(r.y));
  int x1 = int(std::lround(r.x + r.w)), y1 = int(std::lround(r.y + r.h));
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return;
  t = std::min({t, w / 2 + 1, h / 2 + 1});
  fillRect(Rect(x0, y0, w, t), c);
  fillRect(Rect(x0, y1 - t, w, t), c);
  fillRect(Rect(x0, y0 + t, t, h - 2 * t), c);
  fillRect(Rect(x1 - t, y0 + t, t, h - 2 * t), c);
}

void Canvas::fillRoundRect(Rect r, float radius, Color c) {
  int x0 = int(std::lround(r.x)), y0 = int(std::lround(r.y));
  int x1 = int(std::lround(r.x + r.w)), y1 = int(std::lround(r.y + r.h));
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return;
  int rr = std::min(int(std::lround(radius)), std::min(w, h) / 2);
  if (rr <= 0) { fillRect(Rect(x0, y0, w, h), c); return; }
  fillRect(Rect(x0, y0 + rr, w, h - 2 * rr), c);
  fillRect(Rect(x0 + rr, y0, w - 2 * rr, rr), c);
  fillRect(Rect(x0 + rr, y1 - rr, w - 2 * rr, rr), c);
  auto corner = [&](int px0, int py0, float ccx, float ccy) {
    for (int y = py0; y < py0 + rr; y++)
      for (int x = px0; x < px0 + rr; x++) {
        float d = std::hypot(x + 0.5f - ccx, y + 0.5f - ccy) - rr;
        plot(x, y, c, cov255(0.5f - d));
      }
  };
  corner(x0, y0, float(x0 + rr), float(y0 + rr));
  corner(x1 - rr, y0, float(x1 - rr), float(y0 + rr));
  corner(x0, y1 - rr, float(x0 + rr), float(y1 - rr));
  corner(x1 - rr, y1 - rr, float(x1 - rr), float(y1 - rr));
}

void Canvas::strokeRoundRect(Rect r, float radius, Color c, int t) {
  int x0 = int(std::lround(r.x)), y0 = int(std::lround(r.y));
  int x1 = int(std::lround(r.x + r.w)), y1 = int(std::lround(r.y + r.h));
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return;
  int rr = std::min(int(std::lround(radius)), std::min(w, h) / 2);
  if (rr <= 0) { strokeRect(Rect(x0, y0, w, h), c, t); return; }
  fillRect(Rect(x0 + rr, y0, w - 2 * rr, t), c);
  fillRect(Rect(x0 + rr, y1 - t, w - 2 * rr, t), c);
  fillRect(Rect(x0, y0 + rr, t, h - 2 * rr), c);
  fillRect(Rect(x1 - t, y0 + rr, t, h - 2 * rr), c);
  auto corner = [&](int px0, int py0, float ccx, float ccy) {
    for (int y = py0; y < py0 + rr; y++)
      for (int x = px0; x < px0 + rr; x++) {
        float d = std::hypot(x + 0.5f - ccx, y + 0.5f - ccy) - rr;
        plot(x, y, c, cov255(clamp01(0.5f - d) * clamp01(d + t + 0.5f)));
      }
  };
  corner(x0, y0, float(x0 + rr), float(y0 + rr));
  corner(x1 - rr, y0, float(x1 - rr), float(y0 + rr));
  corner(x0, y1 - rr, float(x0 + rr), float(y1 - rr));
  corner(x1 - rr, y1 - rr, float(x1 - rr), float(y1 - rr));
}

void Canvas::fillCircle(Vec2 c, float r, Color col) {
  int x0 = std::max(m_cx0, int(std::floor(c.x - r - 1))), x1 = std::min(m_cx1, int(std::ceil(c.x + r + 1)));
  int y0 = std::max(m_cy0, int(std::floor(c.y - r - 1))), y1 = std::min(m_cy1, int(std::ceil(c.y + r + 1)));
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      float d = std::hypot(x + 0.5f - c.x, y + 0.5f - c.y) - r;
      int cv = cov255(0.5f - d);
      if (cv) plot(x, y, col, cv);
    }
}

void Canvas::strokeCircle(Vec2 c, float r, Color col, float t) {
  float ro = r + t * 0.5f;
  int x0 = std::max(m_cx0, int(std::floor(c.x - ro - 1))), x1 = std::min(m_cx1, int(std::ceil(c.x + ro + 1)));
  int y0 = std::max(m_cy0, int(std::floor(c.y - ro - 1))), y1 = std::min(m_cy1, int(std::ceil(c.y + ro + 1)));
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      float d = std::fabs(std::hypot(x + 0.5f - c.x, y + 0.5f - c.y) - r);
      int cv = cov255(t * 0.5f + 0.5f - d);
      if (cv) plot(x, y, col, cv);
    }
}

void Canvas::line(Vec2 a, Vec2 b, Color col, float th) {
  float dx = b.x - a.x, dy = b.y - a.y;
  if (std::fabs(dx) < 1e-4f && std::fabs(dy) < 1e-4f) { fillCircle(a, th * 0.5f, col); return; }
  bool steep = std::fabs(dy) > std::fabs(dx);
  if (steep) { std::swap(a.x, a.y); std::swap(b.x, b.y); }
  if (a.x > b.x) std::swap(a, b);
  dx = b.x - a.x; dy = b.y - a.y;
  float slope = dy / dx;
  float cosf = 1.f / std::sqrt(1.f + slope * slope);
  float hw = th * 0.5f;
  float ext = hw / cosf + 1.f;
  int xs = int(std::floor(a.x)), xe = int(std::ceil(b.x));
  // clip loop range against the clip rect on the major axis
  int lo = steep ? m_cy0 : m_cx0, hi = steep ? m_cy1 : m_cx1;
  xs = std::max(xs, lo); xe = std::min(xe, hi);
  for (int x = xs; x < xe; x++) {
    float along = std::min(x + 1.f, b.x) - std::max(float(x), a.x);
    if (along <= 0) continue;
    float cx = x + 0.5f;
    float yc = a.y + (cx - a.x) * slope;
    int y0 = int(std::floor(yc - ext)), y1 = int(std::ceil(yc + ext));
    for (int y = y0; y <= y1; y++) {
      float d = std::fabs(y + 0.5f - yc) * cosf;
      int cv = cov255((hw + 0.5f - d) * std::min(1.f, along));
      if (!cv) continue;
      if (steep) plot(y, x, col, cv); else plot(x, y, col, cv);
    }
  }
}

void Canvas::polyline(std::span<const Vec2> p, Color c, float th, bool closed) {
  for (size_t i = 0; i + 1 < p.size(); i++) line(p[i], p[i + 1], c, th);
  if (closed && p.size() > 2) line(p.back(), p.front(), c, th);
  if (th > 1.6f)
    for (auto& v : p) fillCircle(v, th * 0.5f, c);
}

void Canvas::fillPolygon(std::span<const Vec2> p, Color c, bool aa) {
  size_t n = std::min<size_t>(p.size(), 64);
  if (n < 3 || c.a == 0) return;
  float area = 0;
  for (size_t i = 0; i < n; i++) {
    Vec2 a = p[i], b = p[(i + 1) % n];
    area += a.x * b.y - b.x * a.y;
  }
  if (std::fabs(area) < 1e-5f) return;
  float sgn = area > 0 ? 1.f : -1.f;
  struct E { float nx, ny, c; };
  std::array<E, 64> e;
  float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  for (size_t i = 0; i < n; i++) {
    Vec2 a = p[i], b = p[(i + 1) % n];
    float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) e[i] = {0, 0, 1e9f};
    else {
      float nx = -sgn * dy / len, ny = sgn * dx / len;
      e[i] = {nx, ny, -(nx * a.x + ny * a.y)};
    }
    minx = std::min(minx, a.x); maxx = std::max(maxx, a.x);
    miny = std::min(miny, a.y); maxy = std::max(maxy, a.y);
  }
  int x0 = std::max(m_cx0, int(std::floor(minx - 1))), x1 = std::min(m_cx1, int(std::ceil(maxx + 1)));
  int y0 = std::max(m_cy0, int(std::floor(miny - 1))), y1 = std::min(m_cy1, int(std::ceil(maxy + 1)));
  for (int y = y0; y < y1; y++) {
    float py = y + 0.5f;
    uint32_t* row = &m_px[size_t(y) * m_w];
    for (int x = x0; x < x1; x++) {
      float px = x + 0.5f, m = 1e9f;
      for (size_t i = 0; i < n; i++) m = std::min(m, e[i].nx * px + e[i].ny * py + e[i].c);
      int cv = aa ? cov255(m + 0.5f) : (m >= -0.5f ? 255 : 0);
      if (cv) blendPx(row[x], c, effAlpha(c, cv));
    }
  }
}

void Canvas::gradientV(Rect r, Color top, Color bot) {
  int y0 = int(std::lround(r.y)), y1 = int(std::lround(r.y + r.h));
  int n = std::max(1, y1 - y0);
  for (int y = y0; y < y1; y++) {
    float t = n > 1 ? float(y - y0) / float(n - 1) : 0.f;
    fillRect(Rect(r.x, float(y), r.w, 1), top.mix(bot, t));
  }
}

void Canvas::gradientH(Rect r, Color left, Color right) {
  int x0 = int(std::lround(r.x)), x1 = int(std::lround(r.x + r.w));
  int n = std::max(1, x1 - x0);
  for (int x = x0; x < x1; x++) {
    float t = n > 1 ? float(x - x0) / float(n - 1) : 0.f;
    fillRect(Rect(float(x), r.y, 1, r.h), left.mix(right, t));
  }
}

void Canvas::nineSlice(const uint32_t* src, int sw, int sh, Margin margins, Rect dst) {
  if (!src || sw <= 0 || sh <= 0 || dst.w <= 0 || dst.h <= 0) return;

  float ml = margins.l, mr = margins.r, mt = margins.t, mb = margins.b;
  if (ml + mr > dst.w && (ml + mr) > 0.f) {
    float scale = dst.w / (ml + mr);
    ml *= scale;
    mr *= scale;
  }
  if (mt + mb > dst.h && (mt + mb) > 0.f) {
    float scale = dst.h / (mt + mb);
    mt *= scale;
    mb *= scale;
  }

  float sx[4] = {0.f, margins.l, float(sw) - margins.r, float(sw)};
  float sy[4] = {0.f, margins.t, float(sh) - margins.b, float(sh)};
  float dx[4] = {dst.x, dst.x + ml, dst.right() - mr, dst.right()};
  float dy[4] = {dst.y, dst.y + mt, dst.bottom() - mb, dst.bottom()};

  for (int row = 0; row < 3; row++) {
    float sh_slice = sy[row + 1] - sy[row];
    float dh_slice = dy[row + 1] - dy[row];
    if (sh_slice <= 0.f || dh_slice <= 0.f) continue;

    for (int col = 0; col < 3; col++) {
      float sw_slice = sx[col + 1] - sx[col];
      float dw_slice = dx[col + 1] - dx[col];
      if (sw_slice <= 0.f || dw_slice <= 0.f) continue;

      int isx = int(std::lround(sx[col])), isy = int(std::lround(sy[row]));
      int isw = int(std::lround(sw_slice)), ish = int(std::lround(sh_slice));
      const uint32_t* subSrc = src + size_t(isy) * size_t(sw) + size_t(isx);
      blitScaled(subSrc, isw, ish, sw, Rect(dx[col], dy[row], dw_slice, dh_slice));
    }
  }
}

void Canvas::drawBrush(Rect r, const Brush& brush) {
  switch (brush.type) {
  case BrushType::Solid:
    fillRect(r, brush.color);
    break;
  case BrushType::RoundedBox:
    if (brush.radius > 0.0f) fillRoundRect(r, brush.radius, brush.color);
    else fillRect(r, brush.color);
    break;
  case BrushType::Outline:
    if (brush.radius > 0.0f) strokeRoundRect(r, brush.radius, brush.color, int(std::lround(brush.stroke)));
    else strokeRect(r, brush.color, int(std::lround(brush.stroke)));
    break;
  case BrushType::Gradient:
    if (brush.gradientDir == GradientDirection::Horizontal) gradientH(r, brush.color, brush.color2);
    else gradientV(r, brush.color, brush.color2);
    break;
  case BrushType::NineSlice:
    if (brush.imagePixels && brush.imageWidth > 0 && brush.imageHeight > 0) {
      nineSlice(brush.imagePixels, brush.imageWidth, brush.imageHeight, brush.margins, r);
    }
    break;
  }
}

void Canvas::shadow(Rect r, float radius, int spread, Color c) {
  for (int i = spread; i >= 1; i--) {
    float t = 1.f - float(i) / float(spread + 1);
    fillRoundRect(r.inflated(float(i)), radius + float(i), c.withAlpha(uint8_t(std::clamp(c.a * t * t * 0.16f, 0.f, 255.f))));
  }
}

void Canvas::drawGlyph(const Glyph& g, int gx, int base, Color col) {
  int gy = base - g.top;
  if (g.bitmap.empty() || gx >= m_cx1 || gx + g.w <= m_cx0 || gy >= m_cy1 || gy + g.h <= m_cy0) return;
  for (int j = 0; j < g.h; j++) {
    int y = gy + j;
    if (y < m_cy0 || y >= m_cy1) continue;
    uint32_t* row = &m_px[size_t(y) * m_w];
    const uint8_t* src = &g.bitmap[size_t(j) * g.w];
    for (int k = 0; k < g.w; k++) {
      int x = gx + k;
      if (x < m_cx0 || x >= m_cx1 || !src[k]) continue;
      if (!g.argb.empty()) {                              // colour glyph: its own colours, the text colour's alpha
        uint32_t c = g.argb[size_t(j) * g.w + size_t(k)];
        Color cc(uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c), 255);
        blendPx(row[x], cc, int(src[k]) * col.a / 255);
      } else blendPx(row[x], col, effAlpha(col, g_lut.v[src[k]]));
    }
  }
}

void Canvas::text(const Font& f, Vec2 p, std::string_view s, Color col) {
  if (col.a == 0 || s.empty()) return;
  int base = int(std::lround(p.y + f.ascent()));
  if (base - f.ascent() >= m_cy1 || base + f.descent() <= m_cy0) return;
  float pen = p.x;
  for (size_t i = 0; i < s.size();) {
    const Glyph& g = f.glyph(decodeUtf8(s, i));
    drawGlyph(g, int(std::lround(pen)) + g.left, base, col);
    pen += g.advance;
  }
}

void Canvas::glyphRun(const Font& f, Vec2 p, std::span<const GlyphPos> glyphs, Color col) {
  if (col.a == 0 || glyphs.empty()) return;
  float base = p.y + f.ascent();
  for (auto& gp : glyphs) {
    const Glyph& g = f.glyph(gp.key);
    drawGlyph(g, int(std::lround(p.x + gp.x)) + g.left, int(std::lround(base + gp.y)), col);
  }
}

void Canvas::textIn(const Font& f, Rect r, std::string_view s, Color col, HAlign h, bool ellipsis) {
  if (s.empty() || r.w <= 0) return;
  std::string tmp;
  if (ellipsis && f.width(s) > r.w + 0.5f) { tmp = f.ellipsize(s, r.w); s = tmp; }
  float w = f.width(s);
  float x = h == HAlign::Right ? r.right() - w : (h == HAlign::Center ? r.x + (r.w - w) * 0.5f : r.x);
  float baseline = std::floor(r.y + r.h * 0.5f + f.capHeight() * 0.5f + 0.5f);
  pushClip(r);
  text(f, {x, baseline - f.ascent()}, s, col);
  popClip();
}

void Canvas::blit(const uint32_t* src, int sw, int sh, Vec2 dst) {
  int dx = int(std::lround(dst.x)), dy = int(std::lround(dst.y));
  for (int y = 0; y < sh; y++) {
    int ty = dy + y;
    if (ty < m_cy0 || ty >= m_cy1) continue;
    int xs = std::max(0, m_cx0 - dx), xe = std::min(sw, m_cx1 - dx);
    if (xs >= xe) continue;
    std::memcpy(&m_px[size_t(ty) * m_w + dx + xs], src + size_t(y) * sw + xs, size_t(xe - xs) * 4);
  }
}

void Canvas::blitScaled(const uint32_t* src, int sw, int sh, int stride, Rect dst) {
  if (!src || sw <= 0 || sh <= 0 || dst.w < 1 || dst.h < 1) return;
  int dx0 = int(std::lround(dst.x)), dy0 = int(std::lround(dst.y));
  int dw = int(std::lround(dst.w)), dh = int(std::lround(dst.h));
  int y0 = std::max(dy0, m_cy0), y1 = std::min(dy0 + dh, m_cy1);
  int x0 = std::max(dx0, m_cx0), x1 = std::min(dx0 + dw, m_cx1);
  for (int y = y0; y < y1; y++) {
    const uint32_t* row = src + size_t(std::min(sh - 1, (y - dy0) * sh / dh)) * size_t(stride);
    uint32_t* out = &m_px[size_t(y) * m_w];
    for (int x = x0; x < x1; x++) out[x] = row[std::min(sw - 1, (x - dx0) * sw / dw)];
  }
}

void Canvas::fillRaw(Rect r, uint32_t argb) {
  int x0 = std::max(int(std::lround(r.x)), m_cx0), x1 = std::min(int(std::lround(r.right())), m_cx1);
  int y0 = std::max(int(std::lround(r.y)), m_cy0), y1 = std::min(int(std::lround(r.bottom())), m_cy1);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) m_px[size_t(y) * m_w + x] = argb;
}

// ---- Minimal dependency-free PNG writer (stored deflate blocks)
namespace {
uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < n; i++) crc = table[(crc ^ d[i]) & 255] ^ (crc >> 8);
  return ~crc;
}
void be32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); }
void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> out;
  be32(out, uint32_t(data.size()));
  std::vector<uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());
  be32(out, crc32(body.data(), body.size()));
  fwrite(out.data(), 1, out.size(), f);
}
} // namespace

bool Canvas::savePng(const std::string& path) const { return writePng(path, m_px.data(), m_w, m_h, m_w); }

bool Canvas::writePng(const std::string& path, const uint32_t* px, int m_w, int m_h, int stride) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, uint32_t(m_w)); be32(ihdr, uint32_t(m_h));
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(f, "IHDR", ihdr);
  std::vector<uint8_t> raw;
  raw.reserve(size_t(m_w * 3 + 1) * m_h);
  for (int y = 0; y < m_h; y++) {
    raw.push_back(0);
    for (int x = 0; x < m_w; x++) {
      uint32_t p = px[size_t(y) * size_t(stride) + size_t(x)];
      raw.push_back(uint8_t(p >> 16)); raw.push_back(uint8_t(p >> 8)); raw.push_back(uint8_t(p));
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t pos = 0;
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  while (pos < raw.size()) {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n)); z.push_back(uint8_t((~n) >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  be32(z, (b << 16) | a);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
  return true;
}

} // namespace prism
