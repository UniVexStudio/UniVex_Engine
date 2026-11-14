// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Physically-Based Rendering (PBR) Thumbnail Renderer Implementation.
#include "prism/PbrRenderer.h"
#include "prism/Theme.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace prism {

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3f {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  constexpr Vec3f() = default;
  constexpr Vec3f(float v) : x(v), y(v), z(v) {}
  constexpr Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

  Vec3f operator+(const Vec3f& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3f operator-(const Vec3f& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3f operator*(const Vec3f& o) const { return {x * o.x, y * o.y, z * o.z}; }
  Vec3f operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3f operator/(const Vec3f& o) const { return {x / o.x, y / o.y, z / o.z}; }

  float dot(const Vec3f& o) const { return x * o.x + y * o.y + z * o.z; }
  float lengthSq() const { return dot(*this); }
  float length() const { return std::sqrt(lengthSq()); }
  Vec3f normalized() const {
    float len = length();
    return len > 1e-6f ? (*this / len) : Vec3f(0, 0, 1);
  }
};

Vec3f colorToVec(Color c) {
  return Vec3f(float(c.r) / 255.0f, float(c.g) / 255.0f, float(c.b) / 255.0f);
}

uint32_t vecToArgb(const Vec3f& v, float alpha = 1.0f) {
  // Filmic tone mapping (Reinhard extended) & Gamma 2.2 correction
  Vec3f mapped = v / (v + Vec3f(1.0f));
  float r = std::pow(std::clamp(mapped.x, 0.0f, 1.0f), 1.0f / 2.2f);
  float g = std::pow(std::clamp(mapped.y, 0.0f, 1.0f), 1.0f / 2.2f);
  float b = std::pow(std::clamp(mapped.z, 0.0f, 1.0f), 1.0f / 2.2f);
  uint32_t aInt = uint32_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
  uint32_t rInt = uint32_t(r * 255.0f);
  uint32_t gInt = uint32_t(g * 255.0f);
  uint32_t bInt = uint32_t(b * 255.0f);
  return (aInt << 24) | (rInt << 16) | (gInt << 8) | bInt;
}

// GGX / Trowbridge-Reitz Normal Distribution Function
float distributionGGX(const Vec3f& N, const Vec3f& H, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float NdotH = std::max(N.dot(H), 0.0f);
  float NdotH2 = NdotH * NdotH;
  float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
  return a2 / (kPi * denom * denom + 1e-5f);
}

// Schlick Fresnel Approximation
Vec3f fresnelSchlick(float cosTheta, const Vec3f& F0) {
  float f = std::pow(std::clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
  return F0 + (Vec3f(1.0f) - F0) * f;
}

// Smith Geometric Attenuation (Schlick-GGX)
float geometrySchlickGGX(float NdotV, float roughness) {
  float r = (roughness + 1.0f);
  float k = (r * r) / 8.0f;
  return NdotV / (NdotV * (1.0f - k) + k);
}

float geometrySmith(const Vec3f& N, const Vec3f& V, const Vec3f& L, float roughness) {
  float NdotV = std::max(N.dot(V), 0.0f);
  float NdotL = std::max(N.dot(L), 0.0f);
  return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

} // namespace

void PbrRenderer::renderPbrSphere(uint32_t* outPixels, int w, int h, const PbrMaterial& mat) {
  Vec3f albedo = colorToVec(mat.baseColor);
  float metallic = mat.metallic;
  const float gloss = std::clamp(theme().gloss, 0.0f, 1.0f);
  // Low gloss pushes every material towards a satin finish: wider highlights, weaker softbox / bloom.
  float roughness = std::clamp(mat.roughness + (1.0f - gloss) * 0.45f * (1.0f - mat.roughness), 0.04f, 1.0f);

  // F0: 0.04 for dielectrics (plastic/glass), tinted albedo for metals
  Vec3f F0 = Vec3f(0.04f) * (1.0f - metallic) + albedo * metallic;

  float cx = float(w) * 0.5f;
  float cy = float(h) * 0.44f;
  float r = float(std::min(w, h)) * 0.25f;
  float floorY = float(h) - 10.0f;

  // Studio Lighting Setup (Key, Fill, Rim)
  Vec3f lights[3] = {
    Vec3f(-0.55f, 0.70f, 0.65f).normalized(),  // Key softbox
    Vec3f(0.65f, -0.25f, 0.50f).normalized(),  // Fill bounce
    Vec3f(0.0f, 0.90f, -0.45f).normalized()    // Rim / kicker
  };
  Vec3f lightColors[3] = {
    Vec3f(1.0f, 0.98f, 0.95f) * 3.8f,
    Vec3f(0.85f, 0.92f, 1.0f) * 1.2f,
    Vec3f(0.95f, 0.98f, 1.0f) * 2.4f
  };

  Vec3f V(0.0f, 0.0f, 1.0f); // Camera view vector

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      float px = float(x) + 0.5f;
      float py = float(y) + 0.5f;
      int idx = y * w + x;

      // 1. Studio Backdrop (Infinite Studio Cyc)
      float tY = float(y) / float(h);
      Vec3f bgCol = Vec3f(0.030f, 0.040f, 0.060f) * (1.0f - tY) + Vec3f(0.015f, 0.020f, 0.032f) * tY;

      // Studio floor with soft reflection
      if (py >= floorY) {
        float floorDist = (py - floorY) / (float(h) - floorY);
        bgCol = Vec3f(0.020f, 0.027f, 0.040f) * (1.0f - floorDist) + Vec3f(0.012f, 0.016f, 0.026f) * floorDist;
      }

      // Ground Ambient Occlusion shadow under pedestal
      float groundDx = (px - cx) / 28.0f;
      float groundDy = (py - floorY) / 6.0f;
      float groundDist2 = groundDx * groundDx + groundDy * groundDy;
      if (groundDist2 < 1.0f) {
        float shadowOcc = (1.0f - groundDist2);
        bgCol = bgCol * (1.0f - shadowOcc * 0.85f);
      }

      // 2. Titanium Studio Pedestal Disc
      float pedY = floorY - 2.0f;
      float pedDx = (px - cx) / 22.0f;
      float pedDy = (py - pedY) / 6.5f;
      float pedDist2 = pedDx * pedDx + pedDy * pedDy;
      if (pedDist2 <= 1.0f && py >= pedY - 4.0f && py <= floorY + 2.0f) {
        // Pedestal bevel shading
        float rim = std::clamp(1.0f - pedDist2, 0.0f, 1.0f);
        Vec3f pedCol = Vec3f(0.08f, 0.10f, 0.14f) + Vec3f(0.12f, 0.15f, 0.20f) * rim;
        // Pedestal rim highlight
        if (pedDist2 > 0.88f && pedDist2 <= 1.0f) {
          pedCol = pedCol + Vec3f(0.18f, 0.22f, 0.30f);
        }
        bgCol = pedCol;
      }

      // 3. PBR Sphere
      float dx = (px - cx) / r;
      float dy = (py - cy) / r;
      float distSq = dx * dx + dy * dy;

      if (distSq <= 1.0f) {
        float dz = std::sqrt(std::max(0.0f, 1.0f - distSq));
        Vec3f N(dx, -dy, dz); // Sphere surface normal

        // Direct PBR Lighting calculation
        Vec3f Lo(0.0f);

        for (int i = 0; i < 3; i++) {
          Vec3f L = lights[i];
          Vec3f H = (V + L).normalized();

          float NdotL = std::max(N.dot(L), 0.0f);
          float NdotV = std::max(N.dot(V), 0.001f);

          float NDF = distributionGGX(N, H, roughness);
          float G = geometrySmith(N, V, L, roughness);
          Vec3f F = fresnelSchlick(std::max(H.dot(V), 0.0f), F0);

          Vec3f numerator = F * (NDF * G);
          float denominator = 4.0f * NdotV * NdotL + 0.0001f;
          Vec3f specular = numerator / denominator;

          Vec3f kS = F;
          Vec3f kD = (Vec3f(1.0f) - kS) * (1.0f - metallic);

          Lo = Lo + (kD * albedo * (1.0f / kPi) + specular) * lightColors[i] * NdotL;
        }

        // Ambient & Studio IBL environment reflections
        Vec3f R = (N * (2.0f * N.dot(V)) - V).normalized();
        Vec3f F_ambient = fresnelSchlick(std::max(N.dot(V), 0.0f), F0);

        // Studio Horizon line reflection & Softbox reflection
        float horizonReflection = std::clamp(1.0f - std::abs(R.y) * 4.0f, 0.0f, 1.0f);
        float softboxReflection = std::pow(std::max(R.dot(lights[0]), 0.0f), 16.0f / (roughness * roughness + 0.01f));

        Vec3f studioEnv = Vec3f(0.08f, 0.10f, 0.14f);
        studioEnv = studioEnv + Vec3f(0.4f, 0.45f, 0.55f) * (horizonReflection * (1.0f - roughness));
        studioEnv = studioEnv + Vec3f(1.0f, 0.98f, 0.95f) * (softboxReflection * 2.0f * (0.2f + 0.8f * gloss));

        Vec3f ambient = (albedo * 0.06f * (1.0f - metallic)) + (F_ambient * studioEnv);

        // Glass refraction / caustic effect
        if (mat.isGlass) {
          float caustic = std::pow(std::clamp(-N.y, 0.0f, 1.0f), 2.5f) * 0.4f;
          float glassRim = std::pow(1.0f - std::max(N.dot(V), 0.0f), 3.0f);
          Lo = Lo * 0.35f + Vec3f(0.05f, 0.15f, 0.25f) + Vec3f(caustic) + Vec3f(0.5f, 0.8f, 1.0f) * (glassRim * (0.35f + 0.65f * gloss));
        }

        // Emissive core bloom
        Vec3f emissive = colorToVec(mat.emissive);
        if (emissive.lengthSq() > 0.01f) {
          float coreHotspot = std::pow(std::max(N.dot(V), 0.0f), 1.5f);
          Lo = Lo + emissive * (1.5f + coreHotspot * 3.0f);
        }

        Vec3f finalColor = Lo + ambient;

        // Anti-aliased outer edge blending
        float edgeAlpha = std::clamp((1.0f - std::sqrt(distSq)) * r * 1.5f, 0.0f, 1.0f);
        Vec3f blended = finalColor * edgeAlpha + bgCol * (1.0f - edgeAlpha);
        outPixels[idx] = vecToArgb(blended);
      } else {
        // Outside sphere: check for emissive bloom spill
        Vec3f emissive = colorToVec(mat.emissive);
        if (emissive.lengthSq() > 0.01f) {
          float dist = std::sqrt(distSq);
          if (dist < 1.7f) {
            float bloom = std::pow((1.7f - dist) / 0.7f, 2.0f) * 0.45f * (0.2f + 0.8f * gloss);
            bgCol = bgCol + emissive * bloom;
          }
        }
        outPixels[idx] = vecToArgb(bgCol);
      }
    }
  }
}

void PbrRenderer::renderPbrMesh(uint32_t* outPixels, int w, int h, const std::string& meshType) {
  float cx = float(w) * 0.5f;
  float cy = float(h) * 0.44f;
  float floorY = float(h) - 10.0f;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      float px = float(x) + 0.5f;
      float py = float(y) + 0.5f;
      int idx = y * w + x;

      // Studio Cyc Backdrop
      float tY = float(y) / float(h);
      Vec3f bgCol = Vec3f(0.030f, 0.040f, 0.060f) * (1.0f - tY) + Vec3f(0.015f, 0.020f, 0.032f) * tY;
      if (py >= floorY) {
        float floorDist = (py - floorY) / (float(h) - floorY);
        bgCol = Vec3f(0.020f, 0.027f, 0.040f) * (1.0f - floorDist) + Vec3f(0.012f, 0.016f, 0.026f) * floorDist;
      }

      // Ground Contact Shadow
      float gDx = (px - cx) / 22.0f;
      float gDy = (py - floorY) / 5.5f;
      float gDist2 = gDx * gDx + gDy * gDy;
      if (gDist2 < 1.0f) {
        bgCol = bgCol * (1.0f - (1.0f - gDist2) * 0.85f);
      }

      outPixels[idx] = vecToArgb(bgCol);
    }
  }

  // 3D Analytical PBR Mesh Rendering
  if (meshType.find("Cube") != std::string::npos) {
    float s = 17.5f;
    Vec3f albedo(0.20f, 0.50f, 0.90f);
    Vec3f topCol = albedo * 1.6f + Vec3f(0.15f); // Key lit top face
    Vec3f leftCol = albedo * 0.95f;             // Fill lit left face
    Vec3f rightCol = albedo * 0.50f;            // Shadow face

    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float px = float(x) + 0.5f - cx;
        float py = float(y) + 0.5f - cy;
        int idx = y * w + x;

        // Isometric cube bounds
        // Top Face: py < 0 and within top rhomboid
        float absPx = std::abs(px);
        if (py >= -s && py <= 0.0f && absPx <= (s * 0.866f) * (1.0f - (std::abs(py + s * 0.5f) / (s * 0.5f)))) {
          // Chamfer edge highlight
          float edgeDist = std::min(s + py, s * 0.866f - absPx);
          Vec3f col = (edgeDist < 1.2f) ? Vec3f(0.9f, 0.95f, 1.0f) : topCol;
          outPixels[idx] = vecToArgb(col);
        } else if (py > 0.0f && py <= s && px < 0.0f && px >= -s * 0.866f) {
          float slopeBound = (px + s * 0.866f) * (s * 0.5f / (s * 0.866f));
          if (py <= s + slopeBound) {
            float edgeDist = std::abs(px);
            Vec3f col = (edgeDist < 1.0f) ? Vec3f(0.1f, 0.2f, 0.4f) : leftCol;
            outPixels[idx] = vecToArgb(col);
          }
        } else if (py > 0.0f && py <= s && px >= 0.0f && px <= s * 0.866f) {
          float slopeBound = (s * 0.866f - px) * (s * 0.5f / (s * 0.866f));
          if (py <= s + slopeBound) {
            outPixels[idx] = vecToArgb(rightCol);
          }
        }
      }
    }
  } else if (meshType.find("Sphere") != std::string::npos) {
    PbrMaterial meshMat;
    meshMat.baseColor = Color(40, 120, 230);
    meshMat.metallic = 0.1f;
    meshMat.roughness = 0.35f;
    renderPbrSphere(outPixels, w, h, meshMat);

    // Overlay latitude/longitude wireframe guide lines
    float r = float(std::min(w, h)) * 0.25f;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float dx = (float(x) + 0.5f - cx) / r;
        float dy = (float(y) + 0.5f - cy) / r;
        float distSq = dx * dx + dy * dy;
        if (distSq <= 1.0f) {
          // Latitude line at equator
          if (std::abs(dy) < 0.04f || std::abs(dy - 0.5f) < 0.03f || std::abs(dy + 0.5f) < 0.03f) {
            int idx = y * w + x;
            outPixels[idx] = vecToArgb(Vec3f(0.7f, 0.85f, 1.0f), 0.7f);
          }
          // Longitude line at center
          if (std::abs(dx) < 0.04f) {
            int idx = y * w + x;
            outPixels[idx] = vecToArgb(Vec3f(0.7f, 0.85f, 1.0f), 0.7f);
          }
        }
      }
    }
  } else if (meshType.find("Cylinder") != std::string::npos) {
    float cw = 24.0f, ch = 26.0f;
    Vec3f albedo(0.20f, 0.50f, 0.90f);
    Vec3f L = Vec3f(-0.55f, 0.70f, 0.65f).normalized();

    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float px = float(x) + 0.5f - cx;
        float py = float(y) + 0.5f - cy;
        int idx = y * w + x;

        // Top ellipse cap
        float capDx = px / (cw * 0.5f);
        float capDy = (py + ch * 0.5f - 2.0f) / 4.5f;
        if (capDx * capDx + capDy * capDy <= 1.0f) {
          Vec3f col = albedo * 1.5f + Vec3f(0.2f); // Lit top cap
          outPixels[idx] = vecToArgb(col);
          continue;
        }

        // Cylinder body
        if (std::abs(px) <= cw * 0.5f && py >= -ch * 0.5f && py <= ch * 0.5f) {
          float nx = px / (cw * 0.5f);
          float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx));
          Vec3f N(nx, 0.0f, nz);
          float NdotL = std::max(N.dot(L), 0.0f);
          float spec = std::pow(std::max(N.dot((L + Vec3f(0, 0, 1)).normalized()), 0.0f), 12.0f);
          Vec3f col = albedo * (0.3f + NdotL * 1.1f) + Vec3f(spec * 0.8f);
          outPixels[idx] = vecToArgb(col);
        }
      }
    }
  } else if (meshType.find("Cone") != std::string::npos) {
    Vec3f albedo(0.20f, 0.50f, 0.90f);
    float apexY = cy - 18.0f;
    float baseY = cy + 12.0f;
    float baseR = 15.0f;

    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float px = float(x) + 0.5f - cx;
        float py = float(y) + 0.5f;
        int idx = y * w + x;

        if (py >= apexY && py <= baseY) {
          float t = (py - apexY) / (baseY - apexY);
          float curR = baseR * t;
          if (std::abs(px) <= curR) {
            float nx = px / (curR + 1e-4f);
            float NdotL = std::max(-nx * 0.6f + 0.5f, 0.1f);
            float spec = std::pow(std::max(-nx * 0.8f + 0.3f, 0.0f), 16.0f);
            Vec3f col = albedo * (0.2f + NdotL * 1.2f) + Vec3f(spec * 0.9f);
            outPixels[idx] = vecToArgb(col);
          }
        }
      }
    }
  } else {
    // Plane: 3D perspective quad slab
    Vec3f albedo(0.20f, 0.50f, 0.90f);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float px = float(x) + 0.5f - cx;
        float py = float(y) + 0.5f - cy;
        int idx = y * w + x;

        float widthAtY = 22.0f + (py + 8.0f) * 0.8f;
        if (py >= -10.0f && py <= 8.0f && std::abs(px) <= widthAtY) {
          // 4x4 Grid lines
          bool isGridX = (std::abs(px) < 1.0f) || (std::abs(std::abs(px) - widthAtY * 0.5f) < 1.0f);
          bool isGridY = (std::abs(py) < 1.0f) || (std::abs(py + 5.0f) < 1.0f);
          Vec3f col = (isGridX || isGridY) ? Vec3f(0.8f, 0.9f, 1.0f) : (albedo * 0.9f);
          outPixels[idx] = vecToArgb(col);
        } else if (py > 8.0f && py <= 11.0f && std::abs(px) <= widthAtY) {
          // Slab front lip shadow
          outPixels[idx] = vecToArgb(albedo * 0.45f);
        }
      }
    }
  }
}

const uint32_t* PbrRenderer::getCachedThumbnail(const std::string& assetName, int w, int h) {
  static std::map<std::string, std::vector<uint32_t>> s_cache;
  const std::string key = assetName + "|g" + std::to_string(int(std::lround(theme().gloss * 100)));   // re-render when the gloss setting changes
  auto it = s_cache.find(key);
  if (it != s_cache.end()) {
    return it->second.data();
  }

  std::vector<uint32_t>& buffer = s_cache[key];
  buffer.resize(size_t(w * h));

  if (assetName.find(".mat") != std::string::npos || assetName.find("Material") != std::string::npos) {
    PbrMaterial mat;
    if (assetName.find("Gold") != std::string::npos) {
      mat.baseColor = Color(255, 200, 70);
      mat.metallic = 1.0f;
      mat.roughness = 0.15f;
    } else if (assetName.find("Metal") != std::string::npos) {
      mat.baseColor = Color(230, 235, 245);
      mat.metallic = 1.0f;
      mat.roughness = 0.05f;
    } else if (assetName.find("Plastic") != std::string::npos) {
      mat.baseColor = Color(20, 185, 80);
      mat.metallic = 0.0f;
      mat.roughness = 0.28f;
    } else if (assetName.find("Glass") != std::string::npos) {
      mat.baseColor = Color(30, 90, 150);
      mat.metallic = 0.0f;
      mat.roughness = 0.02f;
      mat.isGlass = true;
    } else if (assetName.find("Emissive") != std::string::npos) {
      mat.baseColor = Color(255, 30, 110);
      mat.metallic = 0.0f;
      mat.roughness = 0.5f;
      mat.emissive = Color(255, 40, 120);
    } else {
      mat.baseColor = Color(160, 170, 185);
      mat.metallic = 0.5f;
      mat.roughness = 0.3f;
    }
    renderPbrSphere(buffer.data(), w, h, mat);
  } else if (assetName.find(".mesh") != std::string::npos || assetName.find("Mesh") != std::string::npos) {
    renderPbrMesh(buffer.data(), w, h, assetName);
  }

  return buffer.data();
}

} // namespace prism
