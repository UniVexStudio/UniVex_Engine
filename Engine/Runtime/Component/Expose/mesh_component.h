// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "asset_guid.h"

namespace UVE::Scene {

inline constexpr std::uint32_t kDefaultMeshVisibilityLayerMask = 0x00000001U;
inline constexpr std::uint32_t kMaximumMeshVisibilityLayerIndex = 31U;

/// One of the master spec's named built-in components (Part 7.3). References the mesh and
/// material assets to draw, by GUID — resolved and loaded on demand by MeshRenderer
/// (Part 7.2) via AssetManager/AssetDatabase, matching PrefabInstanceComponent's
/// existing AssetGuid-holding precedent. kInvalidAssetGuid means "no mesh/material
/// assigned" — MeshRenderer skips such entities.
struct MeshComponent final {
    Asset::AssetGuid meshGuid;
    Asset::AssetGuid materialGuid;
    /// Which visibility layers this mesh belongs to, as a bitmask (layer N is bit 1<<N; the
    /// default 0x1 puts every mesh on layer 0, exactly where Godot's VisualInstance3D.layers
    /// starts). Anything that gates rendering by layer masks - the VisibilityRegion3D
    /// membership test, and any future camera cull mask - multiplies across this field with a
    /// single AND. A mesh on NO layers (0) is never managed by a region, which stays an
    /// authoring choice, not an error.
    std::uint32_t visibilityLayers = kDefaultMeshVisibilityLayerMask;
};

/// Validates the authored asset-reference pair without resolving assets. Unassigned, mesh only
/// (drawn with the built-in lit shader until a material is chosen) and both are valid; a material
/// with no mesh is rejected at persistence/runtime boundaries, because it has nothing to draw on.
[[nodiscard]] bool IsMeshComponentValid(const MeshComponent& component) noexcept;

/// True when `component.meshGuid` is assigned (`!= kInvalidAssetGuid`).
[[nodiscard]] bool HasAssignedMeshAsset(const MeshComponent& component) noexcept;

/// True when `component.materialGuid` is assigned (`!= kInvalidAssetGuid`).
[[nodiscard]] bool HasAssignedMaterialAsset(const MeshComponent& component) noexcept;

/// True when `component.visibilityLayers` overlaps `layerMask`.
[[nodiscard]] bool IsMeshVisibleOnLayerMask(const MeshComponent& component,
                                               std::uint32_t layerMask) noexcept;

/// True when bit `layerIndex` (`0..31`) is set in `component.visibilityLayers`.
[[nodiscard]] bool IsMeshOnVisibilityLayer(const MeshComponent& component,
                                              std::uint32_t layerIndex) noexcept;

/// Sets or clears bit `layerIndex` (`0..31`) in `component.visibilityLayers`.
void SetMeshVisibilityLayerBit(MeshComponent& component, std::uint32_t layerIndex,
                                  bool enabled) noexcept;

/// Clears an orphaned `materialGuid` when `meshGuid` is unassigned.
[[nodiscard]] MeshComponent SanitizeMeshComponent(
    const MeshComponent& component) noexcept;

} // namespace UVE::Scene
