// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "multi_mesh_instance_3d.h"
#include "vector3.h"

namespace UVE::Scene {

/// Global or regional foliage wind animation parameters consumed by GPU instanced vegetation.
struct FoliageWindZone3DComponentUVE final {
    Math::Vector3UVE windDirection{1.0F, 0.0F, 0.35F};
    float windStrength = 0.35F;
    float gustFrequency = 1.6F;
    float turbulence = 0.45F;
    float maxSwayMeters = 0.25F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const FoliageWindZone3DComponentUVE&) const = default;
};

struct FoliageScatterSettingsUVE final {
    Math::Vector3UVE areaMin{-10.0F, 0.0F, -10.0F};
    Math::Vector3UVE areaMax{10.0F, 0.0F, 10.0F};
    std::uint32_t count = 256U;
    std::uint32_t seed = 1337U;
    float minScale = 0.8F;
    float maxScale = 1.3F;
};

/// Deterministically scatters foliage transforms into `outMultiMesh.instances` (capped to
/// kMaximumMultiMeshInstances3DUVE) with varied yaw and scale.
void GenerateFoliageScatterInstancesUVE(const FoliageScatterSettingsUVE& settings,
                                        MultiMeshInstance3DComponentUVE& outMultiMesh);

/// Evaluates world-space foliage vertex wind displacement matching `lit_shadowed_3d.glsl`.
[[nodiscard]] Math::Vector3UVE EvaluateFoliageWindDisplacementUVE(
    const Math::Vector3UVE& worldPos, float heightWeight, float timeSeconds,
    const FoliageWindZone3DComponentUVE& wind) noexcept;

} // namespace UVE::Scene
