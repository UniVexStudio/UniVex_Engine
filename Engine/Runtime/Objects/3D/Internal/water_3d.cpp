// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "water_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidColorUVE(const Math::Vector3UVE& c) noexcept {
    return Math::IsFiniteUVE(c) && c.x >= 0.0F && c.y >= 0.0F && c.z >= 0.0F;
}

[[nodiscard]] Math::Vector3UVE ClampColor01UVE(const Math::Vector3UVE& c) noexcept {
    if (!Math::IsFiniteUVE(c)) {
        return Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    }
    return Math::Vector3UVE{
        std::clamp(c.x, 0.0F, 1.0F),
        std::clamp(c.y, 0.0F, 1.0F),
        std::clamp(c.z, 0.0F, 1.0F),
    };
}

struct GerstnerWaveHarmonicUVE {
    float steepness;
    float ampScale;
    float freqScale;
    float speedScale;
    Math::Vector2UVE dir;
};

inline constexpr GerstnerWaveHarmonicUVE kHarmonicsUVE[3] = {
    {0.4F, 0.6F, 1.0F, 1.0F, {0.9578F, 0.2873F}},
    {0.3F, 0.3F, 1.8F, 1.3F, {-0.7071F, 0.7071F}},
    {0.2F, 0.1F, 3.2F, 1.7F, {0.1961F, -0.9806F}},
};

} // namespace

bool IsWater3DComponentValidUVE(const Water3DComponentUVE& value) noexcept {
    return IsValidColorUVE(value.deepColor) &&
           IsValidColorUVE(value.shallowColor) &&
           IsValidColorUVE(value.foamColor) &&
           std::isfinite(value.waveAmplitude) && value.waveAmplitude >= 0.0F &&
           std::isfinite(value.waveFrequency) && value.waveFrequency > 0.0F &&
           std::isfinite(value.waveSpeed) && value.waveSpeed >= 0.0F &&
           std::isfinite(value.clarity) && value.clarity > 0.0F &&
           std::isfinite(value.refractionStrength) && value.refractionStrength >= 0.0F &&
           std::isfinite(value.foamDepth) && value.foamDepth >= 0.0F &&
           std::isfinite(value.metallic) && value.metallic >= 0.0F && value.metallic <= 1.0F &&
           std::isfinite(value.roughness) && value.roughness >= 0.0F && value.roughness <= 1.0F;
}

Water3DComponentUVE SanitizeWater3DComponentUVE(const Water3DComponentUVE& value) noexcept {
    if (IsWater3DComponentValidUVE(value)) {
        return value;
    }
    Water3DComponentUVE sanitized{};
    sanitized.deepColor = ClampColor01UVE(value.deepColor);
    sanitized.shallowColor = ClampColor01UVE(value.shallowColor);
    sanitized.foamColor = ClampColor01UVE(value.foamColor);
    sanitized.waveAmplitude = (std::isfinite(value.waveAmplitude) && value.waveAmplitude >= 0.0F) ? value.waveAmplitude : 0.15F;
    sanitized.waveFrequency = (std::isfinite(value.waveFrequency) && value.waveFrequency > 0.0F) ? value.waveFrequency : 0.80F;
    sanitized.waveSpeed = (std::isfinite(value.waveSpeed) && value.waveSpeed >= 0.0F) ? value.waveSpeed : 1.00F;
    sanitized.clarity = (std::isfinite(value.clarity) && value.clarity > 0.0F) ? value.clarity : 4.00F;
    sanitized.refractionStrength = (std::isfinite(value.refractionStrength) && value.refractionStrength >= 0.0F) ? value.refractionStrength : 0.05F;
    sanitized.foamDepth = (std::isfinite(value.foamDepth) && value.foamDepth >= 0.0F) ? value.foamDepth : 0.30F;
    sanitized.metallic = std::clamp(value.metallic, 0.0F, 1.0F);
    sanitized.roughness = std::clamp(value.roughness, 0.0F, 1.0F);
    return sanitized;
}

bool IsWater3DObjectDefinitionValidUVE(const Water3DObjectDefinitionUVE& value) noexcept {
    return IsWater3DComponentValidUVE(value.water) &&
           IsPrimitiveMeshComponentValidUVE(value.mesh) &&
           value.mesh.kind == PrimitiveMeshKindUVE::Plane;
}

void ApplyWater3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                     const Water3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, Water3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<Water3DComponentUVE>(entity, value.water);
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(entity, value.mesh);
}

float Water3DUVE::EvaluateWaveHeightUVE(const Water3DComponentUVE& water,
                                       const Math::Vector2UVE& xz, const float time) noexcept {
    const float effSpeed = std::max(water.waveSpeed, 0.01F);
    const float effAmp = std::max(water.waveAmplitude, 0.0F);
    const float effFreq = std::max(water.waveFrequency, 0.01F);
    const float t = time * effSpeed;

    float height = 0.0F;
    for (const auto& h : kHarmonicsUVE) {
        const float amp = effAmp * h.ampScale;
        const float freq = effFreq * h.freqScale;
        const float f = freq * ((xz.x * h.dir.x + xz.y * h.dir.y) - h.speedScale * t);
        height += amp * std::sin(f);
    }
    return height;
}

Math::Vector3UVE Water3DUVE::EvaluateWaveNormalUVE(const Water3DComponentUVE& water,
                                                 const Math::Vector2UVE& xz, const float time) noexcept {
    const float effSpeed = std::max(water.waveSpeed, 0.01F);
    const float effAmp = std::max(water.waveAmplitude, 0.0F);
    const float effFreq = std::max(water.waveFrequency, 0.01F);
    const float t = time * effSpeed;

    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;

    for (const auto& h : kHarmonicsUVE) {
        const float amp = effAmp * h.ampScale;
        const float freq = effFreq * h.freqScale;
        const float f = freq * ((xz.x * h.dir.x + xz.y * h.dir.y) - h.speedScale * t);
        const float cosF = std::cos(f);
        const float sinF = std::sin(f);
        const float wa = freq * amp;

        nx -= h.dir.x * wa * cosF;
        ny -= h.steepness * wa * sinF;
        nz -= h.dir.y * wa * cosF;
    }

    const float lenSq = nx * nx + ny * ny + nz * nz;
    if (lenSq > 1.0e-8F) {
        const float invLen = 1.0F / std::sqrt(lenSq);
        return Math::Vector3UVE{nx * invLen, ny * invLen, nz * invLen};
    }
    return Math::Vector3UVE{0.0F, 1.0F, 0.0F};
}

Math::AabbUVE Water3DUVE::LocalBoundsUVE(const Water3DComponentUVE& water) noexcept {
    const float maxDisp = std::max(water.waveAmplitude, 0.05F);
    return Math::AabbUVE::FromCenterExtentsUVE(
        Math::Vector3UVE{0.0F, 0.0F, 0.0F},
        Math::Vector3UVE{0.5F, maxDisp, 0.5F}
    );
}

} // namespace UVE::Scene
