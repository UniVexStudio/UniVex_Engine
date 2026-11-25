// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "asset_guid.h"
#include "entity.h"
#include "color.h"
#include "vector3.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

/// Real-time voxel global illumination volume (`halfExtents`, `subdivisions`, `dynamicRange`,
/// `energy`, `ambientTint`).
struct VoxelGI3DComponent final {
    Math::Vector3 halfExtents{10.0F, 10.0F, 10.0F};
    std::uint32_t subdivisions = 128U;
    float dynamicRange = 2.0F;
    float energy = 1.0F;
    Math::Color ambientTint{1.0F, 1.0F, 1.0F};
    bool enabled = true;

    [[nodiscard]] bool operator==(const VoxelGI3DComponent&) const = default;
};

struct VoxelGI3DObjectDefinition final {
    static constexpr std::string_view defaultName = "VoxelGI";
    VoxelGI3DComponent voxelGi{};
};

void ApplyVoxelGI3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                       const VoxelGI3DObjectDefinition& value);

/// Baked static lightmap volume (`lightmapDataGuid`, `halfExtents`, `energy`, `interior`).
struct LightmapGI3DComponent final {
    Asset::AssetGuid lightmapDataGuid = Asset::kInvalidAssetGuid;
    Math::Vector3 halfExtents{20.0F, 10.0F, 20.0F};
    float energy = 1.0F;
    Math::Color ambientIrradiance{0.8F, 0.85F, 0.9F};
    bool interior = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const LightmapGI3DComponent&) const = default;
};

struct LightmapGI3DObjectDefinition final {
    static constexpr std::string_view defaultName = "LightmapGI";
    LightmapGI3DComponent lightmapGi{};
};

void ApplyLightmapGI3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                          const LightmapGI3DObjectDefinition& value);

/// Manual dynamic-object irradiance probe inside a `LightmapGI` volume (`influenceRadius`,
/// `sampledIrradiance`).
struct LightmapProbe3DComponent final {
    float influenceRadius = 5.0F;
    Math::Color sampledIrradiance{0.5F, 0.5F, 0.5F};
    bool enabled = true;

    [[nodiscard]] bool operator==(const LightmapProbe3DComponent&) const = default;
};

struct LightmapProbe3DObjectDefinition final {
    static constexpr std::string_view defaultName = "LightmapProbe";
    LightmapProbe3DComponent probe{};
};

void ApplyLightmapProbe3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                             const LightmapProbe3DObjectDefinition& value);

/// Evaluates blended indirect GI irradiance at `worldPosition` across all enabled `VoxelGI`,
/// `LightmapGI`, and `LightmapProbe` entities in `entityManager`.
[[nodiscard]] Math::Color EvaluateIndirectGI3DIrradianceAtPoint(
    IEntityManager& entityManager, const Math::Vector3& worldPosition);

} // namespace UVE::Scene
