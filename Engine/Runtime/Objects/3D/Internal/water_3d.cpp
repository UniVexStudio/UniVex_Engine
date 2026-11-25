// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "water_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidColor(const Math::Vector3& c) noexcept {
    return Math::IsFinite(c) && c.x >= 0.0F && c.y >= 0.0F && c.z >= 0.0F;
}

[[nodiscard]] Math::Vector3 ClampColor01(const Math::Vector3& c) noexcept {
    if (!Math::IsFinite(c)) {
        return Math::Vector3{1.0F, 1.0F, 1.0F};
    }
    return Math::Vector3{
        std::clamp(c.x, 0.0F, 1.0F),
        std::clamp(c.y, 0.0F, 1.0F),
        std::clamp(c.z, 0.0F, 1.0F),
    };
}

struct GerstnerWaveHarmonic {
    float steepness;
    float ampScale;
    float freqScale;
    float speedScale;
    Math::Vector2 dir;
};

inline constexpr GerstnerWaveHarmonic kHarmonics[3] = {
    {0.4F, 0.6F, 1.0F, 1.0F, {0.9578F, 0.2873F}},
    {0.3F, 0.3F, 1.8F, 1.3F, {-0.7071F, 0.7071F}},
    {0.2F, 0.1F, 3.2F, 1.7F, {0.1961F, -0.9806F}},
};

} // namespace

bool IsWater3DComponentValid(const Water3DComponent& value) noexcept {
    return IsValidColor(value.deepColor) &&
           IsValidColor(value.shallowColor) &&
           IsValidColor(value.foamColor) &&
           std::isfinite(value.waveAmplitude) && value.waveAmplitude >= 0.0F &&
           std::isfinite(value.waveFrequency) && value.waveFrequency > 0.0F &&
           std::isfinite(value.waveSpeed) && value.waveSpeed >= 0.0F &&
           std::isfinite(value.clarity) && value.clarity > 0.0F &&
           std::isfinite(value.refractionStrength) && value.refractionStrength >= 0.0F &&
           std::isfinite(value.foamDepth) && value.foamDepth >= 0.0F &&
           std::isfinite(value.metallic) && value.metallic >= 0.0F && value.metallic <= 1.0F &&
           std::isfinite(value.roughness) && value.roughness >= 0.0F && value.roughness <= 1.0F;
}

Water3DComponent SanitizeWater3DComponent(const Water3DComponent& value) noexcept {
    if (IsWater3DComponentValid(value)) {
        return value;
    }
    Water3DComponent sanitized{};
    sanitized.deepColor = ClampColor01(value.deepColor);
    sanitized.shallowColor = ClampColor01(value.shallowColor);
    sanitized.foamColor = ClampColor01(value.foamColor);
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

bool IsWater3DObjectDefinitionValid(const Water3DObjectDefinition& value) noexcept {
    return IsWater3DComponentValid(value.water) &&
           IsPrimitiveMeshComponentValid(value.mesh) &&
           value.mesh.kind == PrimitiveMeshKind::Plane;
}

void ApplyWater3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                     const Water3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity, Water3DObjectDefinition::defaultName);
    entityManager.AddComponent<Water3DComponent>(entity, value.water);
    entityManager.AddComponent<PrimitiveMeshComponent>(entity, value.mesh);
}

float Water3D::EvaluateWaveHeight(const Water3DComponent& water,
                                       const Math::Vector2& xz, const float time) noexcept {
    const float effSpeed = std::max(water.waveSpeed, 0.01F);
    const float effAmp = std::max(water.waveAmplitude, 0.0F);
    const float effFreq = std::max(water.waveFrequency, 0.01F);
    const float t = time * effSpeed;

    float height = 0.0F;
    for (const auto& h : kHarmonics) {
        const float amp = effAmp * h.ampScale;
        const float freq = effFreq * h.freqScale;
        const float f = freq * ((xz.x * h.dir.x + xz.y * h.dir.y) - h.speedScale * t);
        height += amp * std::sin(f);
    }
    return height;
}

Math::Vector3 Water3D::EvaluateWaveNormal(const Water3DComponent& water,
                                                 const Math::Vector2& xz, const float time) noexcept {
    const float effSpeed = std::max(water.waveSpeed, 0.01F);
    const float effAmp = std::max(water.waveAmplitude, 0.0F);
    const float effFreq = std::max(water.waveFrequency, 0.01F);
    const float t = time * effSpeed;

    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;

    for (const auto& h : kHarmonics) {
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
        return Math::Vector3{nx * invLen, ny * invLen, nz * invLen};
    }
    return Math::Vector3{0.0F, 1.0F, 0.0F};
}

Math::Aabb Water3D::LocalBounds(const Water3DComponent& water) noexcept {
    const float maxDisp = std::max(water.waveAmplitude, 0.05F);
    return Math::Aabb::FromCenterExtents(
        Math::Vector3{0.0F, 0.0F, 0.0F},
        Math::Vector3{0.5F, maxDisp, 0.5F}
    );
}

} // namespace UVE::Scene
