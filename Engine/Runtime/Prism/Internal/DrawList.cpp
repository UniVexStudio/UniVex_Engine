// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/DrawList.h"
#include <cstring>

namespace prism {

void DrawList::replay(Canvas& c) const {
  for (const DrawCmd& d : cmds) {
    switch (d.op) {
    case DrawOp::Clear: c.clear(d.c); break;
    case DrawOp::PushClip: c.pushClip(d.r); break;
    case DrawOp::PopClip: c.popClip(); break;
    case DrawOp::FillRect: c.fillRect(d.r, d.c); break;
    case DrawOp::FillRoundRect: c.fillRoundRect(d.r, d.f0, d.c); break;
    case DrawOp::StrokeRoundRect: c.strokeRoundRect(d.r, d.f0, d.c, d.i0); break;
    case DrawOp::FillCircle: c.fillCircle(d.a, d.f0, d.c); break;
    case DrawOp::StrokeCircle: c.strokeCircle(d.a, d.f0, d.c, d.r.w); break;
    case DrawOp::Line: c.line(d.a, d.b, d.c, d.f0); break;
    case DrawOp::FillPolygon: c.fillPolygon(std::span<const Vec2>(points.data() + d.i0, size_t(d.i1)), d.c, d.aa); break;
    case DrawOp::GradientV: c.gradientV(d.r, d.c, d.c2); break;
    case DrawOp::GradientH: c.gradientH(d.r, d.c, d.c2); break;
    case DrawOp::NineSlice: c.nineSlice(pixels.data() + d.i0, d.w, d.h, Margin(d.a.x, d.a.y, d.b.x, d.b.y), d.r); break;
    case DrawOp::Text: c.text(*d.font, d.a, std::string_view(text.data() + d.i0, size_t(d.i1)), d.c); break;
    case DrawOp::Blit: c.blit(pixels.data() + d.i0, d.w, d.h, d.a); break;
    case DrawOp::BlitScaled: c.blitScaled(pixels.data() + d.i0, d.w, d.h, d.stride, d.r); break;
    case DrawOp::BlitBlend: c.blitBlend(pixels.data() + d.i0, d.w, d.h, d.stride, d.r); break;
    case DrawOp::FillRaw: c.fillRaw(d.r, d.raw); break;
    case DrawOp::GlyphRun: c.glyphRun(*d.font, d.a, std::span<const Canvas::GlyphPos>(glyphs.data() + d.i0, size_t(d.i1)), d.c); break;
    }
  }
}

void RecordingCanvas::resize(int w, int h) {
  m_w = std::max(1, w);
  m_h = std::max(1, h);
  m_cx0 = m_cy0 = 0;
  m_cx1 = m_w;
  m_cy1 = m_h;
  m_stack.clear();
}

void RecordingCanvas::clear(Color c) { push(DrawOp::Clear).c = c; }

void RecordingCanvas::pushClip(Rect r) {
  Canvas::pushClip(r);        // keep clip() accurate for widgets that query it
  push(DrawOp::PushClip).r = r;
}

void RecordingCanvas::popClip() {
  Canvas::popClip();
  push(DrawOp::PopClip);
}

void RecordingCanvas::fillRect(Rect r, Color c) {
  if (c.a == 0) return;
  DrawCmd& d = push(DrawOp::FillRect);
  d.r = r; d.c = c;
}

void RecordingCanvas::fillRoundRect(Rect r, float radius, Color c) {
  DrawCmd& d = push(DrawOp::FillRoundRect);
  d.r = r; d.f0 = radius; d.c = c;
}

void RecordingCanvas::strokeRoundRect(Rect r, float radius, Color c, int t) {
  DrawCmd& d = push(DrawOp::StrokeRoundRect);
  d.r = r; d.f0 = radius; d.c = c; d.i0 = t;
}

void RecordingCanvas::fillCircle(Vec2 center, float radius, Color c) {
  DrawCmd& d = push(DrawOp::FillCircle);
  d.a = center; d.f0 = radius; d.c = c;
}

void RecordingCanvas::strokeCircle(Vec2 center, float radius, Color c, float t) {
  DrawCmd& d = push(DrawOp::StrokeCircle);
  d.a = center; d.f0 = radius; d.c = c; d.r.w = t; // thickness travels in r.w
}

void RecordingCanvas::line(Vec2 a, Vec2 b, Color c, float t) {
  DrawCmd& d = push(DrawOp::Line);
  d.a = a; d.b = b; d.c = c; d.f0 = t;
}

void RecordingCanvas::fillPolygon(std::span<const Vec2> pts, Color c, bool aa) {
  if (pts.size() < 3 || c.a == 0) return;
  size_t n = std::min<size_t>(pts.size(), 64);
  int off = int(m_list.points.size());
  m_list.points.insert(m_list.points.end(), pts.begin(), pts.begin() + long(n));
  DrawCmd& d = push(DrawOp::FillPolygon);
  d.c = c; d.aa = aa; d.i0 = off; d.i1 = int(n);
}

void RecordingCanvas::gradientV(Rect r, Color top, Color bottom) {
  DrawCmd& d = push(DrawOp::GradientV);
  d.r = r; d.c = top; d.c2 = bottom;
}

void RecordingCanvas::gradientH(Rect r, Color left, Color right) {
  DrawCmd& d = push(DrawOp::GradientH);
  d.r = r; d.c = left; d.c2 = right;
}

void RecordingCanvas::nineSlice(const uint32_t* src, int sw, int sh, Margin margins, Rect dst) {
  if (!src || sw <= 0 || sh <= 0 || dst.w <= 0 || dst.h <= 0) return;
  int off = int(m_list.pixels.size());
  m_list.pixels.insert(m_list.pixels.end(), src, src + size_t(sw) * size_t(sh));
  DrawCmd& d = push(DrawOp::NineSlice);
  d.i0 = off; d.w = sw; d.h = sh; d.r = dst;
  d.a = {margins.l, margins.t};
  d.b = {margins.r, margins.b};
}

void RecordingCanvas::drawBrush(Rect r, const Brush& brush) {
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

void RecordingCanvas::text(const Font& f, Vec2 p, std::string_view s, Color c) {
  if (c.a == 0 || s.empty()) return;
  int off = int(m_list.text.size());
  m_list.text.append(s);
  DrawCmd& d = push(DrawOp::Text);
  d.font = &f; d.a = p; d.c = c; d.i0 = off; d.i1 = int(s.size());
}

void RecordingCanvas::glyphRun(const Font& f, Vec2 p, std::span<const GlyphPos> g, Color c) {
  if (c.a == 0 || g.empty()) return;
  int off = int(m_list.glyphs.size());
  m_list.glyphs.insert(m_list.glyphs.end(), g.begin(), g.end());
  DrawCmd& d = push(DrawOp::GlyphRun);
  d.font = &f; d.a = p; d.c = c; d.i0 = off; d.i1 = int(g.size());
}

void RecordingCanvas::blit(const uint32_t* src, int sw, int sh, Vec2 dst) {
  if (!src || sw <= 0 || sh <= 0) return;
  int off = int(m_list.pixels.size());
  m_list.pixels.insert(m_list.pixels.end(), src, src + size_t(sw) * size_t(sh));
  DrawCmd& d = push(DrawOp::Blit);
  d.i0 = off; d.w = sw; d.h = sh; d.a = dst;
}

void RecordingCanvas::blitScaled(const uint32_t* src, int sw, int sh, int stride, Rect dst) {
  if (!src || sw <= 0 || sh <= 0) return;
  int off = int(m_list.pixels.size());
  m_list.pixels.reserve(m_list.pixels.size() + size_t(sw) * size_t(sh));
  for (int y = 0; y < sh; y++) m_list.pixels.insert(m_list.pixels.end(), src + size_t(y) * size_t(stride), src + size_t(y) * size_t(stride) + size_t(sw));
  DrawCmd& d = push(DrawOp::BlitScaled);
  d.i0 = off; d.w = sw; d.h = sh; d.stride = sw; d.r = dst;
}

void RecordingCanvas::blitBlend(const uint32_t* src, int sw, int sh, int stride, Rect dst) {
  if (!src || sw <= 0 || sh <= 0) return;
  int off = int(m_list.pixels.size());
  m_list.pixels.reserve(m_list.pixels.size() + size_t(sw) * size_t(sh));
  for (int y = 0; y < sh; y++) m_list.pixels.insert(m_list.pixels.end(), src + size_t(y) * size_t(stride), src + size_t(y) * size_t(stride) + size_t(sw));
  DrawCmd& d = push(DrawOp::BlitBlend);
  d.i0 = off; d.w = sw; d.h = sh; d.stride = sw; d.r = dst;
}

void RecordingCanvas::fillRaw(Rect r, uint32_t argb) {
  DrawCmd& d = push(DrawOp::FillRaw);
  d.r = r; d.raw = argb;
}

} // namespace prism
