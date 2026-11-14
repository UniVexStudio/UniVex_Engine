// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Vector Icon & SVG Path definition.
// Supports programmatic construction and SVG path data parsing (M, L, H, V, C, S, Q, T, Z).
#pragma once
#include "Canvas.h"
#include <string_view>
#include <vector>

namespace prism {

enum class PathCmdType : uint8_t {
  MoveTo,
  LineTo,
  QuadTo,
  CubicTo,
  Close
};

struct PathCmd {
  PathCmdType type = PathCmdType::MoveTo;
  Vec2 p0, p1, p2; // p0 is target or ctrl1, p1 is ctrl2 or target, p2 is target
};

class SvgPath {
public:
  SvgPath() = default;

  void moveTo(Vec2 p) { m_cmds.push_back({PathCmdType::MoveTo, p, {}, {}}); }
  void moveTo(float x, float y) { moveTo({x, y}); }
  void lineTo(Vec2 p) { m_cmds.push_back({PathCmdType::LineTo, p, {}, {}}); }
  void lineTo(float x, float y) { lineTo({x, y}); }
  void quadTo(Vec2 ctrl, Vec2 to) { m_cmds.push_back({PathCmdType::QuadTo, ctrl, to, {}}); }
  void quadTo(float cx, float cy, float x, float y) { quadTo({cx, cy}, {x, y}); }
  void cubicTo(Vec2 c1, Vec2 c2, Vec2 to) { m_cmds.push_back({PathCmdType::CubicTo, c1, c2, to}); }
  void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) { cubicTo({c1x, c1y}, {c2x, c2y}, {x, y}); }
  void close() { m_cmds.push_back({PathCmdType::Close, {}, {}, {}}); }

  void clear() { m_cmds.clear(); }
  bool empty() const { return m_cmds.empty(); }
  const std::vector<PathCmd>& commands() const { return m_cmds; }

  // Flattens curves into discrete polylines
  std::vector<std::vector<Vec2>> flatten(float tolerance = 0.5f) const;

  // Draws the vector path to any canvas with optional stroke and fill
  void draw(Canvas& c, Color strokeColor, float strokeWidth = 1.0f, Color fillColor = Color(0, 0, 0, 0), Vec2 offset = {0, 0}, float scale = 1.0f) const;

  // Parse SVG path "d" string attribute
  static SvgPath parse(std::string_view pathData);

private:
  std::vector<PathCmd> m_cmds;
};

} // namespace prism
