// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - DeviceBackend: the integration point for YOUR graphics API (the engine's "Prism" renderer, Vulkan, D3D12, ...).
//
// Implement GpuDevice (eight small calls that map 1:1 onto any modern API) and wrap it:
//
//   class MyPrismDevice : public prism::GpuDevice { ... };               // ~150 lines against your API
//   auto backend = std::make_shared<prism::DeviceBackend>(std::make_shared<MyPrismDevice>(...));
//   app.setBackend(backend);   // or every frame: backend->submit(app.drawList())
//
// DeviceBackend does the rest: batching (buildGpuFrame), glyph-atlas residency (uploads only when it changed),
// image textures (pooled, reused across frames), scissor / blend state per batch, and frame clears.
// Shaders: port kReferenceVertexGlsl / kReferenceFragmentGlsl from GpuBatch.h to your shading language once.
#pragma once
#include "GpuBatch.h"
#include "RenderBackend.h"
#include <memory>
#include <vector>

namespace prism {

enum class GpuTextureFormat : uint8_t { R8, ARGB8 };   // ARGB8: 32-bit 0xAARRGGBB words (the Canvas pixel format)

struct GpuDrawCall {
  uint32_t firstIndex = 0, indexCount = 0;
  int sx = 0, sy = 0, sw = 0, sh = 0;     // scissor, pixels, top-left origin
  bool replace = false;                   // true: write source colour (no blending); false: premultiplied-style "over"
  uint64_t texture = 0;                   // 0 = no texture (solid / SDF shapes)
  GpuMode hint = GpuMode::Solid;          // informational; the per-vertex mode drives the shader
};

class GpuDevice {
public:
  virtual ~GpuDevice() = default;
  virtual uint64_t createTexture(int w, int h, GpuTextureFormat fmt) = 0;                  // returns a non-zero handle
  virtual void updateTexture(uint64_t tex, int w, int h, GpuTextureFormat fmt, const void* data) = 0;
  virtual void destroyTexture(uint64_t tex) = 0;
  virtual void beginFrame(int width, int height) = 0;                                       // bind target, set viewport
  virtual void uploadGeometry(const GpuVertex* v, size_t vertexCount, const uint32_t* idx, size_t indexCount) = 0;
  virtual void clear(Color c, int sx, int sy, int sw, int sh) = 0;                          // scissored clear
  virtual void draw(const GpuDrawCall& call) = 0;
  virtual void endFrame() = 0;                                                              // submit / present
  virtual bool readback(std::vector<uint32_t>& argbOut, int& w, int& h) { (void)argbOut; (void)w; (void)h; return false; }
};

class DeviceBackend : public RenderBackend {
public:
  explicit DeviceBackend(std::shared_ptr<GpuDevice> device, int atlasSize = 2048);
  ~DeviceBackend() override;
  const char* name() const override { return "device"; }
  void resize(int width, int height) override { m_w = width; m_h = height; }
  void submit(const DrawList& list) override;
  PixelView readback() override;
  // stats of the last frame (for profilers / tests)
  struct Stats { size_t batches = 0, draws = 0, clears = 0, vertices = 0, indices = 0, atlasUploads = 0, imageUploads = 0; };
  const Stats& lastFrame() const { return m_stats; }
  size_t totalAtlasUploads() const { return m_totalAtlasUploads; }
  GpuDevice& device() { return *m_dev; }
private:
  std::shared_ptr<GpuDevice> m_dev;
  GlyphAtlas m_atlas;
  GpuFrame m_frame;
  uint64_t m_atlasTex = 0, m_atlasVersion = 0;
  struct PooledImage { uint64_t tex = 0; int w = 0, h = 0; };
  std::vector<PooledImage> m_images;
  Stats m_stats;
  size_t m_totalAtlasUploads = 0;
  int m_w = 0, m_h = 0;
  std::vector<uint32_t> m_readback;
  int m_rbW = 0, m_rbH = 0;
};

} // namespace prism
