// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - GPU batching: turns a DrawList into vertex/index buffers + draw batches that ANY graphics API can render.
//
// A backend (OpenGL reference in GlBackend.cpp, or your own over another API) does only this per frame:
//   1. upload GlyphAtlas pixels if atlas.version changed (R8 texture, atlas.size x atlas.size)
//   2. upload frame.vertices / frame.indices
//   3. for each GpuBatch in order: set scissor, blend (normal | replace), bind texture, draw indexed triangles
//      or clear the target (Kind::Clear)
// and run the fragment program documented in `kReferenceGlsl` below (modes are in GpuMode).
// Output coordinates are pixels with the origin at the top-left; the target is frame.width x frame.height.
#pragma once
#include "DrawList.h"
#include <unordered_map>

namespace prism {

enum class GpuMode : uint8_t { Solid = 0, BoxFill = 1, BoxStroke = 2, Ring = 3, Line = 4, Glyph = 5, Image = 6 };

struct GpuVertex {
  float x, y;        // pixel position
  float u, v;        // atlas / image texel coordinates (0..1)
  uint32_t color;    // 0xAABBGGRR as bytes r,g,b,a in memory order
  float s0[4];       // shape parameters, see GpuMode
  float s1[4];       // s1[3] = mode
};
static_assert(sizeof(GpuVertex) == 52, "GpuVertex layout is part of the backend contract");

struct GpuBatch {
  enum class Kind : uint8_t { Draw, Clear } kind = Kind::Draw;
  enum class Texture : uint8_t { None, Atlas, Image } texture = Texture::None;
  uint32_t firstIndex = 0, indexCount = 0;
  int sx = 0, sy = 0, sw = 0, sh = 0;   // scissor, top-left origin, pixels
  bool replace = false;                 // true: write source colour, no blending (images, host cut-outs)
  int image = -1;                       // index into GpuFrame::images when texture == Image
  Color clearColor;
};

struct GpuImage { int width = 0, height = 0; const uint32_t* argb = nullptr; }; // points into the DrawList

struct GpuFrame {
  int width = 0, height = 0;
  std::vector<GpuVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<GpuBatch> batches;
  std::vector<GpuImage> images;
  void clear() { vertices.clear(); indices.clear(); batches.clear(); images.clear(); }
};

// Packs glyph coverage bitmaps into one R8 texture (shelf packing). Backends upload `pixels` when `version` changes.
class GlyphAtlas {
public:
  struct Entry { int x = 0, y = 0, w = 0, h = 0, left = 0, top = 0; float advance = 0; };
  explicit GlyphAtlas(int size = 2048) : size(size), pixels(size_t(size) * size_t(size), 0) {}
  // Returns null when the atlas is full (the caller resets and retries).
  const Entry* get(const Font& f, char32_t cp);
  void reset();
  int size;
  std::vector<uint8_t> pixels;
  uint64_t version = 1;     // bumps whenever pixels changed
  uint64_t generation = 1;  // bumps on reset (entries invalidated)
private:
  struct Key { const Font* f; char32_t cp; bool operator==(const Key&) const = default; };
  struct KeyHash { size_t operator()(const Key& k) const { return std::hash<const void*>()(k.f) ^ (size_t(k.cp) * 0x9e3779b97f4a7c15ull); } };
  std::unordered_map<Key, Entry, KeyHash> m_map;
  int m_x = 1, m_y = 1, m_rowH = 0;
};

// Builds the GPU frame. Returns false only if it could not fit the glyphs even after resetting the atlas.
bool buildGpuFrame(const DrawList& list, GlyphAtlas& atlas, GpuFrame& out);

// Reference fragment program (GLSL 3.30). `uH` = target height in pixels; p = (gl_FragCoord.x, uH - gl_FragCoord.y).
// Modes: Solid: colour. BoxFill/BoxStroke: rounded-box SDF with s0=(cx,cy,hx,hy), s1.x=radius, s1.y=stroke width.
// Ring: s0=(cx,cy,r,width). Line: s0=(ax,ay,bx,by), s1.x=half width. Glyph: atlas coverage with a 0.8 gamma curve.
// Image: texel copy. These formulas match the CPU rasterizer so both back ends agree to within rounding.
extern const char* const kReferenceVertexGlsl;
extern const char* const kReferenceFragmentGlsl;

} // namespace prism
