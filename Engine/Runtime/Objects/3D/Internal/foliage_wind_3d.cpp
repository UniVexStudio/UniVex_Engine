// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "foliage_wind_3d.h"

#include <algorithm>
#include <cmath>

#include "quaternion.h"
#include "scalar.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] std::uint32_t HashU32UVE(std::uint32_t x) noexcept {
    x ^= x >> 16U;
    x *= 0x7FEB352DU;
    x ^= x >> 15U;
    x *= 0x846CA68BU;
    x ^= x >> 16U;
    return x;
}

[[nodiscard]] float UnitFloatUVE(const std::uint32_t h) noexcept {
    return static_cast<float>(h & 0x00FFFFFFU) / static_cast<float>(0x01000000U);
}

} // namespace

void GenerateFoliageScatterInstancesUVE(const FoliageScatterSettingsUVE& settings,
                                        MultiMeshInstance3DComponentUVE& outMultiMesh) {
    const std::size_t count = std::min<std::size_t>(settings.count, kMaximumMultiMeshInstances3DUVE);
    outMultiMesh.instances.clear();
    outMultiMesh.instances.reserve(count);
    const float loScale = std::max(0.01F, std::min(settings.minScale, settings.maxScale));
    const float hiScale = std::max(loScale, std::max(settings.minScale, settings.maxScale));
    for (std::uint32_t i = 0U; i < static_cast<std::uint32_t>(count); ++i) {
        const float rx = UnitFloatUVE(HashU32UVE(settings.seed ^ (i * 73856093U + 1U)));
        const float rz = UnitFloatUVE(HashU32UVE(settings.seed ^ (i * 19349663U + 2U)));
        const float ryaw = UnitFloatUVE(HashU32UVE(settings.seed ^ (i * 83492791U + 3U)));
        const float rscale = UnitFloatUVE(HashU32UVE(settings.seed ^ (i * 2654435761U + 4U)));

        MultiMeshInstanceTransform3DUVE inst{};
        inst.position = Math::Vector3UVE{
            settings.areaMin.x + (settings.areaMax.x - settings.areaMin.x) * rx,
            settings.areaMin.y + (settings.areaMax.y - settings.areaMin.y) * 0.5F,
            settings.areaMin.z + (settings.areaMax.z - settings.areaMin.z) * rz,
        };
        const float yaw = ryaw * 2.0F * Math::kPiUVE;
        (void)Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, yaw, inst.rotation);
        const float s = loScale + (hiScale - loScale) * rscale;
        inst.scale = Math::Vector3UVE{s, s, s};
        outMultiMesh.instances.push_back(inst);
    }
    outMultiMesh.visibleInstanceCount = -1;
}

Math::Vector3UVE EvaluateFoliageWindDisplacementUVE(const Math::Vector3UVE& worldPos, const float heightWeight,
                                                    const float timeSeconds,
                                                    const FoliageWindZone3DComponentUVE& wind) noexcept {
    if (!wind.enabled || wind.windStrength <= 0.0F || heightWeight <= 0.0F) {
        return Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    const Math::Vector3UVE dir = Math::NormalizeUVE(wind.windDirection);
    const float w = std::clamp(heightWeight, 0.0F, 1.0F);
    const float phase = timeSeconds * wind.gustFrequency + worldPos.x * 0.37F + worldPos.z * 0.29F;
    const float primaryWave = std::sin(phase);
    const float flutter = std::sin(phase * 2.73F + worldPos.y * 1.1F) * wind.turbulence;
    const float disp = std::clamp((primaryWave + flutter) * wind.windStrength * w * w,
                                  -wind.maxSwayMeters, wind.maxSwayMeters);
    return dir * disp;
}

} // namespace UVE::Scene
