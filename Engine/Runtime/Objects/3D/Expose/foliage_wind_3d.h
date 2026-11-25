// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "multi_mesh_instance_3d.h"
#include "vector3.h"

namespace UVE::Scene {

/// Global or regional foliage wind animation parameters consumed by GPU instanced vegetation.
struct FoliageWindZone3DComponent final {
    Math::Vector3 windDirection{1.0F, 0.0F, 0.35F};
    float windStrength = 0.35F;
    float gustFrequency = 1.6F;
    float turbulence = 0.45F;
    float maxSwayMeters = 0.25F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const FoliageWindZone3DComponent&) const = default;
};

struct FoliageScatterSettings final {
    Math::Vector3 areaMin{-10.0F, 0.0F, -10.0F};
    Math::Vector3 areaMax{10.0F, 0.0F, 10.0F};
    std::uint32_t count = 256U;
    std::uint32_t seed = 1337U;
    float minScale = 0.8F;
    float maxScale = 1.3F;
};

/// Deterministically scatters foliage transforms into `outMultiMesh.instances` (capped to
/// kMaximumMultiMeshInstances3D) with varied yaw and scale.
void GenerateFoliageScatterInstances(const FoliageScatterSettings& settings,
                                        MultiMeshInstance3DComponent& outMultiMesh);

/// Evaluates world-space foliage vertex wind displacement matching `lit_shadowed_3d.glsl`.
[[nodiscard]] Math::Vector3 EvaluateFoliageWindDisplacement(
    const Math::Vector3& worldPos, float heightWeight, float timeSeconds,
    const FoliageWindZone3DComponent& wind) noexcept;

} // namespace UVE::Scene
