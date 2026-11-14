// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - render backend interface. A backend consumes DrawLists and produces pixels on some device.
#pragma once
#include "DrawList.h"
#include "Platform.h"
#include <memory>

namespace prism {

class RenderBackend {
public:
  virtual ~RenderBackend() = default;
  virtual const char* name() const = 0;
  virtual void resize(int width, int height) = 0;
  // Executes one frame. A windowed backend presents it; an off-screen one keeps it for readback().
  virtual void submit(const DrawList& list) = 0;
  // Last frame as ARGB pixels (top row first), or an empty view if the backend cannot read back.
  virtual PixelView readback() { return {}; }
};

// Reference backend: replays the list with the CPU rasterizer. Always available, bit-exact with direct painting.
class SoftwareBackend : public RenderBackend {
public:
  const char* name() const override { return "software"; }
  void resize(int w, int h) override { m_canvas.resize(w, h); }
  void submit(const DrawList& l) override {
    if (m_canvas.width() != l.width || m_canvas.height() != l.height) m_canvas.resize(l.width, l.height);
    l.replay(m_canvas);
  }
  PixelView readback() override { return {m_canvas.pixels(), m_canvas.width(), m_canvas.height(), m_canvas.width()}; }
private:
  Canvas m_canvas;
};

} // namespace prism
