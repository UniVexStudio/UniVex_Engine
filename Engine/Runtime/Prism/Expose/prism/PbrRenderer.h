// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Physically-Based Rendering (PBR) Thumbnail Renderer.
// Implements Cook-Torrance GGX microfacet specular, Schlick Fresnel, Smith geometry,
// studio 3-point lighting, HDRI reflections, glass refraction, and emissive bloom.
#pragma once
#include "Core.h"
#include <string>
#include <vector>

namespace prism {

struct PbrMaterial {
  Color baseColor = Color(200, 200, 200);
  float metallic = 0.0f;
  float roughness = 0.5f;
  float ior = 1.5f;
  Color emissive = Color(0, 0, 0, 0);
  bool isGlass = false;
};

class PbrRenderer {
public:
  // Renders a photorealistic PBR material sphere on a titanium studio pedestal into an RGBA buffer.
  static void renderPbrSphere(uint32_t* outPixels, int w, int h, const PbrMaterial& mat);

  // Renders an analytical 3D perspective lit mesh on studio floor into an RGBA buffer.
  static void renderPbrMesh(uint32_t* outPixels, int w, int h, const std::string& meshType);

  // Returns cached RGBA pixel buffer for the specified asset name (e.g. "Gold.mat", "Cube.mesh").
  static const uint32_t* getCachedThumbnail(const std::string& assetName, int w, int h);
};

} // namespace prism
