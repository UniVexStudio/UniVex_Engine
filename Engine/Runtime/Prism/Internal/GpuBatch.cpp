// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/GpuBatch.h"
#include <cmath>

namespace prism {

const char* const kReferenceVertexGlsl = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
layout(location=3) in vec4 aS0;
layout(location=4) in vec4 aS1;
uniform vec2 uSize;
out vec2 vUV; out vec4 vColor; out vec4 vS0; out vec4 vS1;
void main() {
  vUV = aUV; vColor = aColor; vS0 = aS0; vS1 = aS1;
  gl_Position = vec4(aPos.x / uSize.x * 2.0 - 1.0, 1.0 - aPos.y / uSize.y * 2.0, 0.0, 1.0);
}
)";

const char* const kReferenceFragmentGlsl = R"(#version 330 core
in vec2 vUV; in vec4 vColor; in vec4 vS0; in vec4 vS1;
uniform sampler2D uTex;
uniform float uH;
out vec4 oColor;
float cov(float v) { return clamp(v, 0.0, 1.0); }
void main() {
  vec2 p = vec2(gl_FragCoord.x, uH - gl_FragCoord.y);
  int mode = int(vS1.w + 0.5);
  vec4 c = vColor;
  if (mode == 1 || mode == 2) {
    vec2 q = abs(p - vS0.xy) - (vS0.zw - vec2(vS1.x));
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vS1.x;
    float a = cov(0.5 - d);
    if (mode == 2) a *= cov(d + vS1.y + 0.5);
    c.a *= a;
  } else if (mode == 3) {
    float d = abs(length(p - vS0.xy) - vS0.z);
    c.a *= cov(vS0.w * 0.5 + 0.5 - d);
  } else if (mode == 4) {
    vec2 a = vS0.xy, b = vS0.zw, ab = b - a;
    float len = length(ab);
    float dist = abs(ab.x * (p.y - a.y) - ab.y * (p.x - a.x)) / len;
    bool steepAxisY = abs(ab.y) > abs(ab.x);
    float pm = steepAxisY ? p.y : p.x;
    float lo = steepAxisY ? min(a.y, b.y) : min(a.x, b.x);
    float hi = steepAxisY ? max(a.y, b.y) : max(a.x, b.x);
    float px = floor(pm);
    float along = min(px + 1.0, hi) - max(px, lo);
    c.a *= cov((vS1.x + 0.5 - dist) * min(1.0, max(along, 0.0)));
  } else if (mode == 5) {
    float t = texture(uTex, vUV).r;
    c.a *= pow(t, 0.8);
  } else if (mode == 6) {
    c = texture(uTex, vUV).bgra;   // image rows are uploaded as BGRA8; .bgra restores RGBA order
  }
  oColor = c;
}
)";

// ------------------------------------------------------------------ glyph atlas
void GlyphAtlas::reset() {
  m_map.clear();
  std::fill(pixels.begin(), pixels.end(), uint8_t(0));
  m_x = m_y = 1;
  m_rowH = 0;
  generation++;
  version++;
}

const GlyphAtlas::Entry* GlyphAtlas::get(const Font& f, char32_t cp) {
  Key k{&f, cp};
  if (auto it = m_map.find(k); it != m_map.end()) return &it->second;
  const Glyph& g = f.glyph(cp);
  Entry e;
  e.w = g.w; e.h = g.h; e.left = g.left; e.top = g.top; e.advance = g.advance;
  if (!g.bitmap.empty()) {
    if (m_x + g.w + 1 > size) { m_x = 1; m_y += m_rowH + 1; m_rowH = 0; }
    if (m_y + g.h + 1 > size) return nullptr;
    e.x = m_x; e.y = m_y;
    for (int j = 0; j < g.h; j++)
      std::copy_n(&g.bitmap[size_t(j) * size_t(g.w)], g.w, &pixels[size_t(m_y + j) * size_t(size) + size_t(m_x)]);
    m_x += g.w + 1;
    m_rowH = std::max(m_rowH, g.h);
    version++;
  }
  return &m_map.emplace(k, e).first->second;
}

// ------------------------------------------------------------------ batching
namespace {

struct IRect { int x0, y0, x1, y1; };

struct Builder {
  const DrawList& list;
  GlyphAtlas& atlas;
  GpuFrame& out;
  std::array<IRect, 64> stack{};
  int stackTop = 0;
  IRect clip;
  bool atlasFull = false;

  Builder(const DrawList& l, GlyphAtlas& a, GpuFrame& o) : list(l), atlas(a), out(o) { clip = {0, 0, l.width, l.height}; }

  static uint32_t pack(Color c) { return uint32_t(c.r) | (uint32_t(c.g) << 8) | (uint32_t(c.b) << 16) | (uint32_t(c.a) << 24); }

  GpuBatch& batchFor(GpuBatch::Texture tex, int image, bool replace) {
    if (!out.batches.empty()) {
      GpuBatch& b = out.batches.back();
      if (b.kind == GpuBatch::Kind::Draw && b.texture == tex && b.image == image && b.replace == replace &&
          b.sx == clip.x0 && b.sy == clip.y0 && b.sw == clip.x1 - clip.x0 && b.sh == clip.y1 - clip.y0)
        return b;
    }
    GpuBatch b;
    b.texture = tex; b.image = image; b.replace = replace;
    b.firstIndex = uint32_t(out.indices.size());
    b.sx = clip.x0; b.sy = clip.y0; b.sw = clip.x1 - clip.x0; b.sh = clip.y1 - clip.y0;
    out.batches.push_back(b);
    return out.batches.back();
  }

  GpuVertex mk(float x, float y, Color c, GpuMode m, float u = 0, float v = 0) {
    GpuVertex g{};
    g.x = x; g.y = y; g.u = u; g.v = v; g.color = pack(c);
    g.s1[3] = float(m);
    return g;
  }

  void quad(GpuBatch& b, GpuVertex v0, GpuVertex v1, GpuVertex v2, GpuVertex v3) {
    uint32_t base = uint32_t(out.vertices.size());
    out.vertices.push_back(v0); out.vertices.push_back(v1); out.vertices.push_back(v2); out.vertices.push_back(v3);
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) out.indices.push_back(base + i);
    b.indexCount += 6;
  }

  // Axis-aligned quad with shape params copied to all four corners.
  void shapeQuad(GpuBatch& b, float x0, float y0, float x1, float y1, Color c, GpuMode m, const float s0[4], const float s1x, const float s1y) {
    GpuVertex v[4] = {mk(x0, y0, c, m), mk(x1, y0, c, m), mk(x1, y1, c, m), mk(x0, y1, c, m)};
    for (auto& g : v) { for (int i = 0; i < 4; i++) g.s0[i] = s0[i]; g.s1[0] = s1x; g.s1[1] = s1y; }
    quad(b, v[0], v[1], v[2], v[3]);
  }

  void fillRect(Rect r, Color c) {
    if (c.a == 0) return;
    int x0 = std::max(clip.x0, int(std::lround(r.x))), y0 = std::max(clip.y0, int(std::lround(r.y)));
    int x1 = std::min(clip.x1, int(std::lround(r.x + r.w))), y1 = std::min(clip.y1, int(std::lround(r.y + r.h)));
    if (x0 >= x1 || y0 >= y1) return;
    GpuBatch& b = batchFor(GpuBatch::Texture::None, -1, false);
    quad(b, mk(float(x0), float(y0), c, GpuMode::Solid), mk(float(x1), float(y0), c, GpuMode::Solid),
         mk(float(x1), float(y1), c, GpuMode::Solid), mk(float(x0), float(y1), c, GpuMode::Solid));
  }

  void roundRect(Rect r, float radius, Color c, int stroke /*0 = fill*/) {
    int x0 = int(std::lround(r.x)), y0 = int(std::lround(r.y)), x1 = int(std::lround(r.x + r.w)), y1 = int(std::lround(r.y + r.h));
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0 || c.a == 0) return;
    int rr = std::min(int(std::lround(radius)), std::min(w, h) / 2);
    if (stroke == 0 && rr <= 0) { fillRect(Rect(float(x0), float(y0), float(w), float(h)), c); return; }
    if (stroke > 0) stroke = std::min({stroke, w / 2 + 1, h / 2 + 1});
    float s0[4] = {float(x0 + x1) * 0.5f, float(y0 + y1) * 0.5f, float(w) * 0.5f, float(h) * 0.5f};
    GpuBatch& b = batchFor(GpuBatch::Texture::None, -1, false);
    shapeQuad(b, float(x0), float(y0), float(x1), float(y1), c, stroke ? GpuMode::BoxStroke : GpuMode::BoxFill, s0, float(rr), float(stroke));
  }

  void circle(Vec2 ctr, float r, Color c) {
    float s0[4] = {ctr.x, ctr.y, r, r};
    GpuBatch& b = batchFor(GpuBatch::Texture::None, -1, false);
    shapeQuad(b, ctr.x - r - 1, ctr.y - r - 1, ctr.x + r + 1, ctr.y + r + 1, c, GpuMode::BoxFill, s0, r, 0);
  }

  void ring(Vec2 ctr, float r, Color c, float t) {
    float ro = r + t * 0.5f + 1;
    float s0[4] = {ctr.x, ctr.y, r, t};
    GpuBatch& b = batchFor(GpuBatch::Texture::None, -1, false);
    shapeQuad(b, ctr.x - ro, ctr.y - ro, ctr.x + ro, ctr.y + ro, c, GpuMode::Ring, s0, 0, 0);
  }

  void line(Vec2 a, Vec2 bb, Color c, float th) {
    float dx = bb.x - a.x, dy = bb.y - a.y;
    if (std::fabs(dx) < 1e-4f && std::fabs(dy) < 1e-4f) { circle(a, th * 0.5f, c); return; }
    float len = std::sqrt(dx * dx + dy * dy), ux = dx / len, uy = dy / len;
    float hw = th * 0.5f, ext = hw + 1.5f;
    float nx = -uy * ext, ny = ux * ext;
    // extend one pixel along the line so end pixels (partial columns) are covered
    Vec2 p0{a.x - ux, a.y - uy}, p1{bb.x + ux, bb.y + uy};
    float s0[4] = {a.x, a.y, bb.x, bb.y};
    GpuBatch& b = batchFor(GpuBatch::Texture::None, -1, false);
    GpuVertex v[4] = {mk(p0.x + nx, p0.y + ny, c, GpuMode::Line), mk(p1.x + nx, p1.y + ny, c, GpuMode::Line),
                      mk(p1.x - nx, p1.y - ny, c, GpuMode::Line), mk(p0.x - nx, p0.y - ny, c, GpuMode::Line)};
    for (auto& g : v) { for (int i = 0; i < 4; i++) g.s0[i] = s0[i]; g.s1[0] = hw; }
    quad(b, v[0], v[1], v[2], v[3]);
  }

  // Convex polygon: inner fan at -0.5px, 1px feather ring fading to 0 (matches coverage = clamp(dist + 0.5)).
  void polygon(const Vec2* p, int n, Color c, bool aa) {
    if (n < 3 || c.a == 0) return;
    float area = 0;
    for (int i = 0; i < n; i++) { Vec2 a = p[i], b = p[(i + 1) % n]; area += a.x * b.y - b.x * a.y; }
    if (std::fabs(area) < 1e-5f) return;
    float sgn = area > 0 ? 1.f : -1.f;
    Vec2 nrm[64];
    for (int i = 0; i < n; i++) {
      Vec2 a = p[(i + n - 1) % n], b = p[i], d = p[(i + 1) % n];
      auto en = [&](Vec2 s, Vec2 e) { float dx = e.x - s.x, dy = e.y - s.y, l = std::sqrt(dx * dx + dy * dy); return l < 1e-6f ? Vec2{0, 0} : Vec2{sgn * dy / l, -sgn * dx / l}; };
      Vec2 n1 = en(a, b), n2 = en(b, d);
      Vec2 m{n1.x + n2.x, n1.y + n2.y};
      float d2 = m.x * m.x + m.y * m.y;
      if (d2 < 1e-6f) m = n1; else { float k = 2.f / d2; m = {m.x * k, m.y * k}; }   // miter, so edges move by exactly 1 unit
      float ml = std::sqrt(m.x * m.x + m.y * m.y);
      if (ml > 3.f) { m.x *= 3.f / ml; m.y *= 3.f / ml; }
      nrm[size_t(i)] = m;
    }
    GpuBatch& b = batchFor(GpuBatch::Texture::None, -1, false);
    uint32_t base = uint32_t(out.vertices.size());
    Color clear = c.withAlpha(0);
    float inset = aa ? -0.5f : 0.5f;       // non-AA expands by half a pixel, like the CPU path
    for (int i = 0; i < n; i++) out.vertices.push_back(mk(p[i].x + nrm[size_t(i)].x * inset, p[i].y + nrm[size_t(i)].y * inset, c, GpuMode::Solid));
    for (int i = 1; i + 1 < n; i++) { out.indices.push_back(base); out.indices.push_back(base + uint32_t(i)); out.indices.push_back(base + uint32_t(i) + 1); b.indexCount += 3; }
    if (aa) {
      uint32_t ob = uint32_t(out.vertices.size());
      for (int i = 0; i < n; i++) out.vertices.push_back(mk(p[i].x + nrm[size_t(i)].x * 0.5f, p[i].y + nrm[size_t(i)].y * 0.5f, clear, GpuMode::Solid));
      for (int i = 0; i < n; i++) {
        uint32_t j = uint32_t((i + 1) % n), k = uint32_t(i);
        for (uint32_t v : {base + k, ob + k, ob + j, base + k, ob + j, base + j}) out.indices.push_back(v);
        b.indexCount += 6;
      }
    }
  }

  // One glyph at pen position (pixel x, baseline y). Colour glyphs become image quads (blended).
  void glyph(const Font& f, char32_t key, float px, float baseY, Color c) {
    const Glyph& g = f.glyph(key);
    if (!g.argb.empty() && g.w > 0 && g.h > 0) {
      float gx = float(int(std::lround(px)) + g.left), gy = float(int(std::lround(baseY)) - g.top);
      int idx = int(out.images.size());
      out.images.push_back({g.w, g.h, g.argb.data()});
      GpuBatch& b = batchFor(GpuBatch::Texture::Image, idx, false);
      Color w(255, 255, 255, c.a);
      quad(b, mk(gx, gy, w, GpuMode::Image, 0, 0), mk(gx + float(g.w), gy, w, GpuMode::Image, 1, 0),
           mk(gx + float(g.w), gy + float(g.h), w, GpuMode::Image, 1, 1), mk(gx, gy + float(g.h), w, GpuMode::Image, 0, 1));
      return;
    }
    const GlyphAtlas::Entry* e = atlas.get(f, key);
    if (!e) { atlasFull = true; return; }
    if (e->w <= 0) return;
    float gx = float(int(std::lround(px)) + e->left), gy = float(int(std::lround(baseY)) - e->top);
    float inv = 1.f / float(atlas.size);
    float u0 = float(e->x) * inv, v0 = float(e->y) * inv, u1 = float(e->x + e->w) * inv, v1 = float(e->y + e->h) * inv;
    GpuBatch& b = batchFor(GpuBatch::Texture::Atlas, -1, false);
    quad(b, mk(gx, gy, c, GpuMode::Glyph, u0, v0), mk(gx + float(e->w), gy, c, GpuMode::Glyph, u1, v0),
         mk(gx + float(e->w), gy + float(e->h), c, GpuMode::Glyph, u1, v1), mk(gx, gy + float(e->h), c, GpuMode::Glyph, u0, v1));
  }

  void text(const DrawCmd& d) {
    const Font& f = *d.font;
    std::string_view s(list.text.data() + d.i0, size_t(d.i1));
    int base = int(std::lround(d.a.y + f.ascent()));
    float pen = d.a.x;
    for (size_t i = 0; i < s.size() && !atlasFull;) {
      char32_t cp = decodeUtf8(s, i);
      glyph(f, cp, pen, float(base), d.c);
      pen += f.glyph(cp).advance;
    }
  }

  void glyphRun(const DrawCmd& d) {
    const Font& f = *d.font;
    float base = d.a.y + f.ascent();
    for (int i = 0; i < d.i1 && !atlasFull; i++) {
      const Canvas::GlyphPos& g = list.glyphs[size_t(d.i0 + i)];
      glyph(f, g.key, d.a.x + g.x, base + g.y, d.c);
    }
  }

  void image(const DrawCmd& d, Rect dst, bool replace = true) {
    int x0 = int(std::lround(dst.x)), y0 = int(std::lround(dst.y)), x1 = int(std::lround(dst.x + dst.w)), y1 = int(std::lround(dst.y + dst.h));
    if (x1 <= x0 || y1 <= y0) return;
    int idx = int(out.images.size());
    out.images.push_back({d.w, d.h, list.pixels.data() + d.i0});
    GpuBatch& b = batchFor(GpuBatch::Texture::Image, idx, replace);
    Color w(255, 255, 255, 255);
    quad(b, mk(float(x0), float(y0), w, GpuMode::Image, 0, 0), mk(float(x1), float(y0), w, GpuMode::Image, 1, 0),
         mk(float(x1), float(y1), w, GpuMode::Image, 1, 1), mk(float(x0), float(y1), w, GpuMode::Image, 0, 1));
  }

  void raw(Rect r, uint32_t argb) {
    int x0 = std::max(clip.x0, int(std::lround(r.x))), y0 = std::max(clip.y0, int(std::lround(r.y)));
    int x1 = std::min(clip.x1, int(std::lround(r.right()))), y1 = std::min(clip.y1, int(std::lround(r.bottom())));
    if (x0 >= x1 || y0 >= y1) return;
    Color c(uint8_t(argb >> 16), uint8_t(argb >> 8), uint8_t(argb), uint8_t(argb >> 24));
    GpuBatch& b = batchFor(GpuBatch::Texture::None, -1, true);
    quad(b, mk(float(x0), float(y0), c, GpuMode::Solid), mk(float(x1), float(y0), c, GpuMode::Solid),
         mk(float(x1), float(y1), c, GpuMode::Solid), mk(float(x0), float(y1), c, GpuMode::Solid));
  }

  void gradientV(Rect r, Color top, Color bot) {
    if (top.a == 0 && bot.a == 0) return;
    int x0 = std::max(clip.x0, int(std::lround(r.x))), y0 = std::max(clip.y0, int(std::lround(r.y)));
    int x1 = std::min(clip.x1, int(std::lround(r.x + r.w))), y1 = std::min(clip.y1, int(std::lround(r.y + r.h)));
    if (x0 >= x1 || y0 >= y1) return;
    GpuBatch& b = batchFor(GpuBatch::Texture::None, -1, false);
    float ry0 = float(std::lround(r.y));
    float h = float(std::lround(r.y + r.h)) - ry0;
    Color c_top = h > 0.f ? top.mix(bot, (float(y0) - ry0) / h) : top;
    Color c_bot = h > 0.f ? top.mix(bot, (float(y1) - ry0) / h) : bot;
    quad(b, mk(float(x0), float(y0), c_top, GpuMode::Solid),
            mk(float(x1), float(y0), c_top, GpuMode::Solid),
            mk(float(x1), float(y1), c_bot, GpuMode::Solid),
            mk(float(x0), float(y1), c_bot, GpuMode::Solid));
  }

  void gradientH(Rect r, Color left, Color right) {
    if (left.a == 0 && right.a == 0) return;
    int x0 = std::max(clip.x0, int(std::lround(r.x))), y0 = std::max(clip.y0, int(std::lround(r.y)));
    int x1 = std::min(clip.x1, int(std::lround(r.x + r.w))), y1 = std::min(clip.y1, int(std::lround(r.y + r.h)));
    if (x0 >= x1 || y0 >= y1) return;
    GpuBatch& b = batchFor(GpuBatch::Texture::None, -1, false);
    float rx0 = float(std::lround(r.x));
    float w = float(std::lround(r.x + r.w)) - rx0;
    Color c_left = w > 0.f ? left.mix(right, (float(x0) - rx0) / w) : left;
    Color c_right = w > 0.f ? left.mix(right, (float(x1) - rx0) / w) : right;
    quad(b, mk(float(x0), float(y0), c_left, GpuMode::Solid),
            mk(float(x1), float(y0), c_right, GpuMode::Solid),
            mk(float(x1), float(y1), c_right, GpuMode::Solid),
            mk(float(x0), float(y1), c_left, GpuMode::Solid));
  }

  void nineSlice(const DrawCmd& d) {
    Margin m(d.a.x, d.a.y, d.b.x, d.b.y);
    Rect dst = d.r;
    int sw = d.w, sh = d.h;
    if (sw <= 0 || sh <= 0 || dst.w <= 0 || dst.h <= 0) return;

    float ml = m.l, mr = m.r, mt = m.t, mb = m.b;
    if (ml + mr > dst.w && (ml + mr) > 0.f) {
      float scale = dst.w / (ml + mr);
      ml *= scale; mr *= scale;
    }
    if (mt + mb > dst.h && (mt + mb) > 0.f) {
      float scale = dst.h / (mt + mb);
      mt *= scale; mb *= scale;
    }

    float sx[4] = {0.f, m.l, float(sw) - m.r, float(sw)};
    float sy[4] = {0.f, m.t, float(sh) - m.b, float(sh)};
    float dx[4] = {dst.x, dst.x + ml, dst.right() - mr, dst.right()};
    float dy[4] = {dst.y, dst.y + mt, dst.bottom() - mb, dst.bottom()};

    int idx = int(out.images.size());
    out.images.push_back({sw, sh, list.pixels.data() + d.i0});
    GpuBatch& b = batchFor(GpuBatch::Texture::Image, idx, true);
    Color white(255, 255, 255, 255);
    float invW = 1.0f / float(sw), invH = 1.0f / float(sh);

    for (int row = 0; row < 3; row++) {
      float dh = dy[row + 1] - dy[row];
      if (dh <= 0.f) continue;
      float v0 = sy[row] * invH, v1 = sy[row + 1] * invH;
      for (int col = 0; col < 3; col++) {
        float dw = dx[col + 1] - dx[col];
        if (dw <= 0.f) continue;
        float u0 = sx[col] * invW, u1 = sx[col + 1] * invW;
        quad(b, mk(dx[col], dy[row], white, GpuMode::Image, u0, v0),
                mk(dx[col + 1], dy[row], white, GpuMode::Image, u1, v0),
                mk(dx[col + 1], dy[row + 1], white, GpuMode::Image, u1, v1),
                mk(dx[col], dy[row + 1], white, GpuMode::Image, u0, v1));
      }
    }
  }

  void run() {
    for (const DrawCmd& d : list.cmds) {
      switch (d.op) {
      case DrawOp::Clear: {
        GpuBatch b;
        b.kind = GpuBatch::Kind::Clear;
        b.clearColor = d.c;
        out.batches.push_back(b);
        break;
      }
      case DrawOp::PushClip: {
        if (stackTop < 64) stack[size_t(stackTop++)] = clip;
        int x0 = std::max(clip.x0, int(std::lround(d.r.x))), y0 = std::max(clip.y0, int(std::lround(d.r.y)));
        int x1 = std::min(clip.x1, int(std::lround(d.r.x + d.r.w))), y1 = std::min(clip.y1, int(std::lround(d.r.y + d.r.h)));
        clip = {x0, y0, std::max(x0, x1), std::max(y0, y1)};
        break;
      }
      case DrawOp::PopClip:
        if (stackTop > 0) clip = stack[size_t(--stackTop)];
        break;
      case DrawOp::FillRect: fillRect(d.r, d.c); break;
      case DrawOp::FillRoundRect: roundRect(d.r, d.f0, d.c, 0); break;
      case DrawOp::StrokeRoundRect: roundRect(d.r, d.f0, d.c, d.i0); break;
      case DrawOp::FillCircle: circle(d.a, d.f0, d.c); break;
      case DrawOp::StrokeCircle: ring(d.a, d.f0, d.c, d.r.w); break;
      case DrawOp::Line: line(d.a, d.b, d.c, d.f0); break;
      case DrawOp::FillPolygon: polygon(list.points.data() + d.i0, d.i1, d.c, d.aa); break;
      case DrawOp::GradientV: gradientV(d.r, d.c, d.c2); break;
      case DrawOp::GradientH: gradientH(d.r, d.c, d.c2); break;
      case DrawOp::NineSlice: nineSlice(d); break;
      case DrawOp::Text: text(d); break;
      case DrawOp::Blit: image(d, Rect(d.a.x, d.a.y, float(d.w), float(d.h))); break;
      case DrawOp::BlitScaled: image(d, d.r); break;
      case DrawOp::BlitBlend: image(d, d.r, false); break;
      case DrawOp::FillRaw: raw(d.r, d.raw); break;
      case DrawOp::GlyphRun: glyphRun(d); break;
      }
      if (atlasFull) return;
    }
  }
};

} // namespace

bool buildGpuFrame(const DrawList& list, GlyphAtlas& atlas, GpuFrame& out) {
  for (int attempt = 0; attempt < 2; attempt++) {
    out.clear();
    out.width = list.width;
    out.height = list.height;
    Builder b(list, atlas, out);
    b.run();
    if (!b.atlasFull) return true;
    atlas.reset();           // atlas full: start over with only what this frame needs
  }
  return false;
}

} // namespace prism
