// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/color_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/trs_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::size_t kMaximumMultiMeshInstances3DUVE = 16384U;

struct MultiMeshInstanceTransform3DUVE final {
    Math::Vector3UVE position{0.0F, 0.0F, 0.0F};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
    Math::ColorUVE customColor{1.0F, 1.0F, 1.0F};

    [[nodiscard]] bool operator==(const MultiMeshInstanceTransform3DUVE&) const = default;
};

/// Mass-instanced 3D mesh component built on `SurfaceInstance3DBaseUVE`. Stores a shared
/// `meshAssetGuid`, `materialAssetGuid`, `visibleInstanceCount` (`-1` draws all instances), and
/// per-instance local transforms (`instances`).
struct MultiMeshInstance3DComponentUVE final {
    Asset::AssetGuidUVE meshAssetGuid = Asset::kInvalidAssetGuidUVE;
    Asset::AssetGuidUVE materialAssetGuid = Asset::kInvalidAssetGuidUVE;
    std::int32_t visibleInstanceCount = -1;
    std::vector<MultiMeshInstanceTransform3DUVE> instances{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const MultiMeshInstance3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsMultiMeshInstance3DComponentValidUVE(
    const MultiMeshInstance3DComponentUVE& value) noexcept;

[[nodiscard]] MultiMeshInstance3DComponentUVE SanitizeMultiMeshInstance3DComponentUVE(
    const MultiMeshInstance3DComponentUVE& value);

struct MultiMeshInstance3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "MultiMeshInstance3D";
    MultiMeshInstance3DComponentUVE multiMesh{};
};

[[nodiscard]] bool IsMultiMeshInstance3DObjectDefinitionValidUVE(
    const MultiMeshInstance3DObjectDefinitionUVE& value) noexcept;

/// Applies `ApplySurfaceInstance3DBaseUVE` (giving the node `RenderInstanceComponentUVE` +
/// `SurfaceInstanceComponentUVE`) and attaches `MultiMeshInstance3DComponentUVE`.
void ApplyMultiMeshInstance3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const MultiMeshInstance3DObjectDefinitionUVE& value);

class MultiMeshInstance3DUVE final {
public:
    MultiMeshInstance3DUVE() = delete;

    /// Returns the number of instances that should be drawn (`min(visibleInstanceCount, instances.size())`
    /// when `visibleInstanceCount >= 0`, else `instances.size()`).
    [[nodiscard]] static std::size_t ResolveActiveInstanceCountUVE(
        const MultiMeshInstance3DComponentUVE& multiMesh) noexcept;

    /// Builds the world-space 4x4 model matrix for instance `index` under `ownerWorldTransform`.
    [[nodiscard]] static Math::Matrix4x4UVE BuildInstanceWorldMatrixUVE(
        const MultiMeshInstance3DComponentUVE& multiMesh, std::size_t index,
        const WorldTransformComponentUVE& ownerWorldTransform) noexcept;

    /// Computes the combined world-space AABB enclosing all active instances (assuming each
    /// instance has `instanceLocalHalfExtents`).
    [[nodiscard]] static Math::AabbUVE ComputeCombinedWorldBoundsUVE(
        const MultiMeshInstance3DComponentUVE& multiMesh,
        const WorldTransformComponentUVE& ownerWorldTransform,
        const Math::Vector3UVE& instanceLocalHalfExtents = Math::Vector3UVE{0.5F, 0.5F, 0.5F}) noexcept;
};

} // namespace UVE::Scene
