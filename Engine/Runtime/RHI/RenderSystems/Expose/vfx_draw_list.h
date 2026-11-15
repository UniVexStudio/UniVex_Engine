// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vector2.h"
#include "vector3.h"

namespace UVE::Render {

inline constexpr std::size_t kMaximumVfxSpriteInstancesPerFrameUVE = 1'000'000U;
inline constexpr std::size_t kVfxSpriteVerticesPerQuadUVE = 6U;

/// 48-byte GPU instance record uploaded to `layout(std430, binding = 0)` for instanced sprite drawing.
struct alignas(16) VfxSpriteGpuInstanceUVE final {
    float posX = 0.0F;
    float posY = 0.0F;
    float posZ = 0.0F;
    float rotationRad = 0.0F;

    float velX = 0.0F;
    float velY = 0.0F;
    float velZ = 0.0F;
    float frame = 0.0F;

    float sizeX = 1.0F;
    float sizeY = 1.0F;
    float packedColorBits = 0.0F;
    float colorHdrScale = 1.0F;
};
static_assert(sizeof(VfxSpriteGpuInstanceUVE) == 48U,
              "VfxSpriteGpuInstanceUVE must match GLSL std430 VfxSpriteInstanceData (3 x vec4 = 48 bytes)");

/// CPU-expanded sprite vertex used by the vertex-attribute fallback path (`UVE_VFX_VERTEX_FALLBACK`).
struct VfxSpriteFallbackVertexUVE final {
    Math::Vector3UVE position{};
    Math::Vector2UVE uv{};
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;
    float frame = 0.0F;
    float pad0 = 0.0F;
};

/// Packs linear RGBA color (with separate HDR RGB multiplier) into an `unpackUnorm4x8`-compatible 32-bit word.
[[nodiscard]] inline std::uint32_t PackVfxRgba8UnormUVE(const float r,
                                                        const float g,
                                                        const float b,
                                                        const float a,
                                                        float* const outHdrScale = nullptr) noexcept {
    const float safeR = std::isfinite(r) ? std::max(0.0F, r) : 0.0F;
    const float safeG = std::isfinite(g) ? std::max(0.0F, g) : 0.0F;
    const float safeB = std::isfinite(b) ? std::max(0.0F, b) : 0.0F;
    const float safeA = std::isfinite(a) ? std::clamp(a, 0.0F, 1.0F) : 0.0F;
    const float maxRgb = std::max({safeR, safeG, safeB, 1.0F});
    if (outHdrScale != nullptr) {
        *outHdrScale = maxRgb;
    }
    const float invScale = 1.0F / maxRgb;
    const auto toByte = [](const float v) noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(std::lround(v * 255.0F), 0L, 255L));
    };
    const std::uint32_t r8 = toByte(safeR * invScale);
    const std::uint32_t g8 = toByte(safeG * invScale);
    const std::uint32_t b8 = toByte(safeB * invScale);
    const std::uint32_t a8 = toByte(safeA);
    return r8 | (g8 << 8U) | (b8 << 16U) | (a8 << 24U);
}

/// Unpacks a 32-bit `unpackUnorm4x8` word and multiplies RGB by `hdrScale`.
inline void UnpackVfxRgba8UnormUVE(const std::uint32_t packed,
                                   const float hdrScale,
                                   float& outR,
                                   float& outG,
                                   float& outB,
                                   float& outA) noexcept {
    const float scale = (std::isfinite(hdrScale) ? std::max(0.0F, hdrScale) : 1.0F) / 255.0F;
    outR = static_cast<float>(packed & 0xFFU) * scale;
    outG = static_cast<float>((packed >> 8U) & 0xFFU) * scale;
    outB = static_cast<float>((packed >> 16U) & 0xFFU) * scale;
    outA = static_cast<float>((packed >> 24U) & 0xFFU) / 255.0F;
}

/// Creates a `VfxSpriteGpuInstanceUVE` from unpacked particle attributes.
[[nodiscard]] inline VfxSpriteGpuInstanceUVE MakeVfxSpriteGpuInstanceUVE(
    const Math::Vector3UVE& position,
    const float rotationRadians,
    const Math::Vector3UVE& velocity,
    const float frame,
    const Math::Vector2UVE& size,
    const float r,
    const float g,
    const float b,
    const float a) noexcept {
    float hdrScale = 1.0F;
    const std::uint32_t packed = PackVfxRgba8UnormUVE(r, g, b, a, &hdrScale);
    VfxSpriteGpuInstanceUVE inst{};
    inst.posX = position.x;
    inst.posY = position.y;
    inst.posZ = position.z;
    inst.rotationRad = rotationRadians;
    inst.velX = velocity.x;
    inst.velY = velocity.y;
    inst.velZ = velocity.z;
    inst.frame = frame;
    inst.sizeX = size.x;
    inst.sizeY = size.y;
    inst.packedColorBits = std::bit_cast<float>(packed);
    inst.colorHdrScale = hdrScale;
    return inst;
}

/// Single emitter-renderer batch extracted from `VfxWorldUVE` for `Renderer3DUVE`.
struct VfxSpriteBatchUVE final {
    std::int32_t facing = 0;      ///< 0 CameraPlane, 1 Spherical, 2 Velocity, 3 FixedAxis.
    std::int32_t blend = 0;       ///< 0 Additive, 1 AlphaBlend (Premultiplied), 2 OpaqueCutout.
    std::int32_t rows = 1;
    std::int32_t cols = 1;
    std::int32_t frameBlend = 0;
    float stretchScale = 0.1F;
    float alphaCutoff = 0.5F;
    float softDistance = 0.0F;
    float cameraFadeNear = 0.0F;
    float cameraFadeFar = 0.0F;
    float emissive = 1.0F;
    Math::Vector3UVE axis{0.0F, 1.0F, 0.0F};
    std::string texturePath;
    std::string materialPath;
    std::uint64_t attachEntityKey = 0ULL;

    std::vector<VfxSpriteGpuInstanceUVE> instances;
    std::vector<float> sortDepths;
};

/// Frame-level extraction and rendering statistics for the VFX sprite pipeline.
struct VfxDrawStatsUVE final {
    std::size_t batchesExtracted = 0U;
    std::size_t opaqueBatchesExtracted = 0U;
    std::size_t translucentBatchesExtracted = 0U;
    std::size_t instancesExtracted = 0U;
    std::size_t instancesTruncated = 0U;
};

/// Per-frame collection of sprite batches extracted from `VfxWorldUVE`.
struct VfxDrawListUVE final {
    std::vector<VfxSpriteBatchUVE> batches;
    VfxDrawStatsUVE stats{};

    void ClearUVE() noexcept {
        batches.clear();
        stats = VfxDrawStatsUVE{};
    }

    [[nodiscard]] bool HasOpaqueBatchesUVE() const noexcept { return stats.opaqueBatchesExtracted > 0U; }
    [[nodiscard]] bool HasTranslucentBatchesUVE() const noexcept { return stats.translucentBatchesExtracted > 0U; }
};

} // namespace UVE::Render
