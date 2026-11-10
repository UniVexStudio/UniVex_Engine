// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - icon pipeline: SVG sources -> multi-DPI coverage atlas -> tinted draw.
// Icons are authored as SVG (path data + viewBox), baked once per scale into a single packed atlas
// (8-bit coverage) and drawn tinted at any colour. Baking is deterministic, so the atlas can also be
// written out (`savePng`) as a build artifact.
#pragma once
#include "SvgPath.h"
#include <map>
#include <string>
#include <vector>

namespace prism {

class IconAtlas {
public:
  struct Entry { int x = 0, y = 0, w = 0, h = 0; float scale = 1; };
  // -- sources
  // `paths` = one or more SVG "d" strings. viewBox is square (24 for the common icon sets).
  void add(const std::string& name, std::vector<std::string> paths, float viewBox = 24, float stroke = 1.5f, bool fill = false);
  // Parses an .svg document: every <path d="..">, the viewBox, and stroke-width / fill="none" hints.
  bool addSvg(const std::string& name, const std::string& svgText);
  // Manifest: one icon per line, "name | viewBox | stroke | fill(0/1) | d [; d ...]" (# comments allowed). Returns icons added.
  int loadManifest(const std::string& text);
  // -- bake
  void bake(const std::vector<float>& scales = {1.f, 1.5f, 2.f}, int baseSize = 16);
  bool baked() const { return m_w > 0; }
  size_t iconCount() const { return m_src.size(); }
  bool has(const std::string& name) const { return m_src.count(name) > 0; }
  std::vector<std::string> names() const;
  // -- lookup / draw
  const Entry* find(const std::string& name, float scale) const;   // entry whose scale is closest to `scale`
  uint8_t coverage(const Entry& e, int x, int y) const { return m_cov[size_t(e.y + y) * size_t(m_w) + size_t(e.x + x)]; }
  // Draws into `box` (nearest scale entry, resampled if the box differs). Returns false if unknown.
  bool draw(Canvas& c, const std::string& name, Rect box, Color tint, float scale = 1.f) const;
  int width() const { return m_w; }
  int height() const { return m_h; }
  bool savePng(const std::string& path) const;
private:
  struct Src { std::vector<SvgPath> paths; float vb = 24, stroke = 1.5f; bool fill = false; };
  std::map<std::string, Src> m_src;
  std::map<std::string, std::vector<Entry>> m_entries;
  std::vector<uint8_t> m_cov;
  int m_w = 0, m_h = 0;
};

} // namespace prism
