// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Brush definitions for styled rendering.
// Supports solid colors, rounded boxes, outlines, linear gradients (V/H), and 9-slice images.
#pragma once
#include "Core.h"

namespace prism {

enum class BrushType : uint8_t {
  Solid,
  RoundedBox,
  Outline,
  Gradient,
  NineSlice,
};

enum class GradientDirection : uint8_t {
  Vertical,
  Horizontal,
  Diagonal,
};

struct Brush {
  BrushType type = BrushType::Solid;
  Color color = Color(255, 255, 255, 255);
  Color color2 = Color(255, 255, 255, 255);
  float radius = 0.0f;
  float stroke = 1.0f;
  GradientDirection gradientDir = GradientDirection::Vertical;
  Margin margins;
  const uint32_t* imagePixels = nullptr;
  int imageWidth = 0;
  int imageHeight = 0;
  int imageStride = 0;

  static Brush solid(Color c) {
    Brush b;
    b.type = BrushType::Solid;
    b.color = c;
    return b;
  }

  static Brush rounded(Color c, float r) {
    Brush b;
    b.type = BrushType::RoundedBox;
    b.color = c;
    b.radius = r;
    return b;
  }

  static Brush outline(Color c, float stroke = 1.0f, float radius = 0.0f) {
    Brush b;
    b.type = BrushType::Outline;
    b.color = c;
    b.stroke = stroke;
    b.radius = radius;
    return b;
  }

  static Brush gradient(Color c0, Color c1, GradientDirection dir = GradientDirection::Vertical) {
    Brush b;
    b.type = BrushType::Gradient;
    b.color = c0;
    b.color2 = c1;
    b.gradientDir = dir;
    return b;
  }

  static Brush gradientV(Color c0, Color c1) {
    return gradient(c0, c1, GradientDirection::Vertical);
  }

  static Brush gradientH(Color c0, Color c1) {
    return gradient(c0, c1, GradientDirection::Horizontal);
  }

  static Brush nineSlice(const uint32_t* px, int w, int h, Margin margins, int stride = 0) {
    Brush b;
    b.type = BrushType::NineSlice;
    b.imagePixels = px;
    b.imageWidth = w;
    b.imageHeight = h;
    b.imageStride = stride > 0 ? stride : w;
    b.margins = margins;
    return b;
  }
};

} // namespace prism
