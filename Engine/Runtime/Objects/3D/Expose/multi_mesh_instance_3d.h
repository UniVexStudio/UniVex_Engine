// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "asset_guid.h"
#include "entity.h"
#include "world_transform_component.h"
#include "aabb.h"
#include "color.h"
#include "matrix4x4.h"
#include "trs.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

inline constexpr std::size_t kMaximumMultiMeshInstances3D = 16384U;

struct MultiMeshInstanceTransform3D final {
    Math::Vector3 position{0.0F, 0.0F, 0.0F};
    Math::Quaternion rotation{};
    Math::Vector3 scale{1.0F, 1.0F, 1.0F};
    Math::Color customColor{1.0F, 1.0F, 1.0F};

    [[nodiscard]] bool operator==(const MultiMeshInstanceTransform3D&) const = default;
};

/// Mass-instanced 3D mesh component built on `SurfaceInstance3DBase`. Stores a shared
/// `meshAssetGuid`, `materialAssetGuid`, `visibleInstanceCount` (`-1` draws all instances), and
/// per-instance local transforms (`instances`).
struct MultiMeshInstance3DComponent final {
    Asset::AssetGuid meshAssetGuid = Asset::kInvalidAssetGuid;
    Asset::AssetGuid materialAssetGuid = Asset::kInvalidAssetGuid;
    std::int32_t visibleInstanceCount = -1;
    std::vector<MultiMeshInstanceTransform3D> instances{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const MultiMeshInstance3DComponent&) const = default;
};

[[nodiscard]] bool IsMultiMeshInstance3DComponentValid(
    const MultiMeshInstance3DComponent& value) noexcept;

[[nodiscard]] MultiMeshInstance3DComponent SanitizeMultiMeshInstance3DComponent(
    const MultiMeshInstance3DComponent& value);

struct MultiMeshInstance3DObjectDefinition final {
    static constexpr std::string_view defaultName = "MultiMeshInstance3D";
    MultiMeshInstance3DComponent multiMesh{};
};

[[nodiscard]] bool IsMultiMeshInstance3DObjectDefinitionValid(
    const MultiMeshInstance3DObjectDefinition& value) noexcept;

/// Applies `ApplySurfaceInstance3DBase` (giving the node `RenderInstanceComponent` +
/// `SurfaceInstanceComponent`) and attaches `MultiMeshInstance3DComponent`.
void ApplyMultiMeshInstance3DObjectDefinition(
    IEntityManager& entityManager, Entity entity,
    const MultiMeshInstance3DObjectDefinition& value);

class MultiMeshInstance3D final {
public:
    MultiMeshInstance3D() = delete;

    /// Returns the number of instances that should be drawn (`min(visibleInstanceCount, instances.size())`
    /// when `visibleInstanceCount >= 0`, else `instances.size()`).
    [[nodiscard]] static std::size_t ResolveActiveInstanceCount(
        const MultiMeshInstance3DComponent& multiMesh) noexcept;

    /// Builds the world-space 4x4 model matrix for instance `index` under `ownerWorldTransform`.
    [[nodiscard]] static Math::Matrix4x4 BuildInstanceWorldMatrix(
        const MultiMeshInstance3DComponent& multiMesh, std::size_t index,
        const WorldTransformComponent& ownerWorldTransform) noexcept;

    /// Computes the combined world-space AABB enclosing all active instances (assuming each
    /// instance has `instanceLocalHalfExtents`).
    [[nodiscard]] static Math::Aabb ComputeCombinedWorldBounds(
        const MultiMeshInstance3DComponent& multiMesh,
        const WorldTransformComponent& ownerWorldTransform,
        const Math::Vector3& instanceLocalHalfExtents = Math::Vector3{0.5F, 0.5F, 0.5F}) noexcept;
};

} // namespace UVE::Scene
