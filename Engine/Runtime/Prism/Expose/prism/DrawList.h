// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - DrawList: a recorded, backend-independent description of one UI frame.
//
// Widgets paint through the Canvas interface. RecordingCanvas implements that interface by appending
// commands to a DrawList instead of touching pixels. A RenderBackend (software, OpenGL, or one you write for
// Vulkan / D3D / Metal) then executes the list. The list owns all of its data (strings, points, image pixels),
// so it can be handed to another thread and outlives the widget tree that produced it.
#pragma once
#include "Canvas.h"

namespace prism {

enum class DrawOp : uint8_t {
  Clear, PushClip, PopClip,
  FillRect, FillRoundRect, StrokeRoundRect, FillCircle, StrokeCircle, Line, FillPolygon,
  GradientV, GradientH, NineSlice,
  Text, Blit, BlitScaled, FillRaw, GlyphRun, BlitBlend,
};

struct DrawCmd {
  DrawOp op = DrawOp::FillRect;
  bool aa = true;               // FillPolygon
  Rect r;                       // rect / clip / destination
  Vec2 a, b;                    // circle centre (a), line end points (a, b) / margins (a=lt, b=rb)
  Color c, c2;                  // colour; c2 = bottom colour of GradientV, right colour of GradientH
  float f0 = 0;                 // radius or thickness
  int i0 = 0;                   // integer thickness | offset into points / text bytes / pixels
  int i1 = 0;                   // count (points / bytes)
  int w = 0, h = 0, stride = 0; // image size
  const Font* font = nullptr;   // Text
  uint32_t raw = 0;             // FillRaw colour
};

struct DrawList {
  int width = 0, height = 0;
  std::vector<DrawCmd> cmds;
  std::vector<Vec2> points;
  std::string text;
  std::vector<uint32_t> pixels;
  std::vector<Canvas::GlyphPos> glyphs;   // GlyphRun payload

  void clear() { cmds.clear(); points.clear(); text.clear(); pixels.clear(); glyphs.clear(); }
  void reserve(size_t cmdCount, size_t pointCount = 0, size_t textBytes = 0, size_t pixelCount = 0) {
    if (cmdCount > cmds.capacity()) cmds.reserve(cmdCount);
    if (pointCount > points.capacity()) points.reserve(pointCount);
    if (textBytes > text.capacity()) text.reserve(textBytes);
    if (pixelCount > pixels.capacity()) pixels.reserve(pixelCount);
  }
  // Executes the list on any canvas. Replaying onto a software Canvas reproduces direct painting exactly.
  void replay(Canvas& c) const;
  size_t commandCount() const { return cmds.size(); }
};

// Canvas that records instead of drawing.
class RecordingCanvas : public Canvas {
public:
  DrawList& list() { return m_list; }
  const DrawList& list() const { return m_list; }
  void beginFrame() { m_list.clear(); m_list.width = m_w; m_list.height = m_h; m_cx0 = m_cy0 = 0; m_cx1 = m_w; m_cy1 = m_h; m_stack.clear(); }

  void resize(int w, int h) override;
  void clear(Color c) override;
  void pushClip(Rect r) override;
  void popClip() override;
  void fillRect(Rect r, Color c) override;
  void fillRoundRect(Rect r, float radius, Color c) override;
  void strokeRoundRect(Rect r, float radius, Color c, int thickness = 1) override;
  void fillCircle(Vec2 center, float radius, Color c) override;
  void strokeCircle(Vec2 center, float radius, Color c, float thickness = 1) override;
  void line(Vec2 a, Vec2 b, Color c, float thickness = 1) override;
  void fillPolygon(std::span<const Vec2> pts, Color c, bool aa = true) override;
  void gradientV(Rect r, Color top, Color bottom) override;
  void gradientH(Rect r, Color left, Color right) override;
  void nineSlice(const uint32_t* src, int sw, int sh, Margin margins, Rect dst) override;
  void drawBrush(Rect r, const Brush& brush) override;
  void text(const Font& f, Vec2 p, std::string_view s, Color c) override;
  void glyphRun(const Font& f, Vec2 p, std::span<const GlyphPos> glyphs, Color c) override;
  void blit(const uint32_t* src, int sw, int sh, Vec2 dst) override;
  void blitScaled(const uint32_t* src, int sw, int sh, int stride, Rect dst) override;
  void blitBlend(const uint32_t* src, int sw, int sh, int stride, Rect dst) override;
  void fillRaw(Rect r, uint32_t argb) override;

private:
  DrawCmd& push(DrawOp op) { m_list.cmds.emplace_back(); m_list.cmds.back().op = op; return m_list.cmds.back(); }
  DrawList m_list;
};

} // namespace prism
