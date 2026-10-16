// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/color_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Real-time voxel global illumination volume (`halfExtents`, `subdivisions`, `dynamicRange`,
/// `energy`, `ambientTint`).
struct VoxelGI3DComponentUVE final {
    Math::Vector3UVE halfExtents{10.0F, 10.0F, 10.0F};
    std::uint32_t subdivisions = 128U;
    float dynamicRange = 2.0F;
    float energy = 1.0F;
    Math::ColorUVE ambientTint{1.0F, 1.0F, 1.0F};
    bool enabled = true;

    [[nodiscard]] bool operator==(const VoxelGI3DComponentUVE&) const = default;
};

struct VoxelGI3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "VoxelGI";
    VoxelGI3DComponentUVE voxelGi{};
};

void ApplyVoxelGI3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const VoxelGI3DObjectDefinitionUVE& value);

/// Baked static lightmap volume (`lightmapDataGuid`, `halfExtents`, `energy`, `interior`).
struct LightmapGI3DComponentUVE final {
    Asset::AssetGuidUVE lightmapDataGuid = Asset::kInvalidAssetGuidUVE;
    Math::Vector3UVE halfExtents{20.0F, 10.0F, 20.0F};
    float energy = 1.0F;
    Math::ColorUVE ambientIrradiance{0.8F, 0.85F, 0.9F};
    bool interior = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const LightmapGI3DComponentUVE&) const = default;
};

struct LightmapGI3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "LightmapGI";
    LightmapGI3DComponentUVE lightmapGi{};
};

void ApplyLightmapGI3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const LightmapGI3DObjectDefinitionUVE& value);

/// Manual dynamic-object irradiance probe inside a `LightmapGI` volume (`influenceRadius`,
/// `sampledIrradiance`).
struct LightmapProbe3DComponentUVE final {
    float influenceRadius = 5.0F;
    Math::ColorUVE sampledIrradiance{0.5F, 0.5F, 0.5F};
    bool enabled = true;

    [[nodiscard]] bool operator==(const LightmapProbe3DComponentUVE&) const = default;
};

struct LightmapProbe3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "LightmapProbe";
    LightmapProbe3DComponentUVE probe{};
};

void ApplyLightmapProbe3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                             const LightmapProbe3DObjectDefinitionUVE& value);

/// Evaluates blended indirect GI irradiance at `worldPosition` across all enabled `VoxelGI`,
/// `LightmapGI`, and `LightmapProbe` entities in `entityManager`.
[[nodiscard]] Math::ColorUVE EvaluateIndirectGI3DIrradianceAtPointUVE(
    IEntityManagerUVE& entityManager, const Math::Vector3UVE& worldPosition);

} // namespace UVE::Scene
