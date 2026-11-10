// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/GpuDevice.h"

namespace prism {

DeviceBackend::DeviceBackend(std::shared_ptr<GpuDevice> device, int atlasSize) : m_dev(std::move(device)), m_atlas(atlasSize) {}

DeviceBackend::~DeviceBackend() {
  if (!m_dev) return;
  if (m_atlasTex) m_dev->destroyTexture(m_atlasTex);
  for (auto& im : m_images) if (im.tex) m_dev->destroyTexture(im.tex);
}

void DeviceBackend::submit(const DrawList& list) {
  m_stats = {};
  if (!m_dev) return;
  m_frame.clear();
  buildGpuFrame(list, m_atlas, m_frame);
  // glyph atlas: one R8 texture, re-uploaded only when glyphs were added (or the atlas was reset)
  if (!m_atlasTex) m_atlasTex = m_dev->createTexture(m_atlas.size, m_atlas.size, GpuTextureFormat::R8);
  if (m_atlasVersion != m_atlas.version) {
    m_dev->updateTexture(m_atlasTex, m_atlas.size, m_atlas.size, GpuTextureFormat::R8, m_atlas.pixels.data());
    m_atlasVersion = m_atlas.version;
    m_stats.atlasUploads++;
    m_totalAtlasUploads++;
  }
  // images (blits, thumbnails, host viewports): a pool of textures reused by slot, resized only when needed
  if (m_images.size() < m_frame.images.size()) m_images.resize(m_frame.images.size());
  for (size_t i = 0; i < m_frame.images.size(); i++) {
    const GpuImage& src = m_frame.images[i];
    PooledImage& dst = m_images[i];
    if (!src.argb || src.width <= 0 || src.height <= 0) continue;
    if (dst.tex && (dst.w != src.width || dst.h != src.height)) { m_dev->destroyTexture(dst.tex); dst.tex = 0; }
    if (!dst.tex) { dst.tex = m_dev->createTexture(src.width, src.height, GpuTextureFormat::ARGB8); dst.w = src.width; dst.h = src.height; }
    m_dev->updateTexture(dst.tex, src.width, src.height, GpuTextureFormat::ARGB8, src.argb);
    m_stats.imageUploads++;
  }
  m_dev->beginFrame(m_frame.width, m_frame.height);
  m_dev->uploadGeometry(m_frame.vertices.data(), m_frame.vertices.size(), m_frame.indices.data(), m_frame.indices.size());
  m_stats.vertices = m_frame.vertices.size();
  m_stats.indices = m_frame.indices.size();
  m_stats.batches = m_frame.batches.size();
  for (auto& b : m_frame.batches) {
    if (b.kind == GpuBatch::Kind::Clear) { m_dev->clear(b.clearColor, b.sx, b.sy, b.sw, b.sh); m_stats.clears++; continue; }
    if (!b.indexCount) continue;
    GpuDrawCall dc;
    dc.firstIndex = b.firstIndex; dc.indexCount = b.indexCount;
    dc.sx = b.sx; dc.sy = b.sy; dc.sw = b.sw; dc.sh = b.sh;
    dc.replace = b.replace;
    dc.texture = b.texture == GpuBatch::Texture::Atlas ? m_atlasTex
               : b.texture == GpuBatch::Texture::Image && b.image >= 0 && size_t(b.image) < m_images.size() ? m_images[size_t(b.image)].tex : 0;
    dc.hint = b.texture == GpuBatch::Texture::Atlas ? GpuMode::Glyph : b.texture == GpuBatch::Texture::Image ? GpuMode::Image : GpuMode::Solid;
    m_dev->draw(dc);
    m_stats.draws++;
  }
  m_dev->endFrame();
}

PixelView DeviceBackend::readback() {
  if (!m_dev || !m_dev->readback(m_readback, m_rbW, m_rbH) || m_readback.size() < size_t(m_rbW) * size_t(m_rbH)) return {};
  return {m_readback.data(), m_rbW, m_rbH, m_rbW};
}

} // namespace prism
