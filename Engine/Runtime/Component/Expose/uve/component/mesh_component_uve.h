// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/asset/asset_guid_uve.h"

namespace UVE::Scene {

/// One of the master spec's named built-in components (Part 7.3). References the mesh and
/// material assets to draw, by GUID — resolved and loaded on demand by MeshRendererUVE
/// (Part 7.2) via AssetManagerUVE/AssetDatabaseUVE, matching PrefabInstanceComponentUVE's
/// existing AssetGuidUVE-holding precedent. kInvalidAssetGuidUVE means "no mesh/material
/// assigned" — MeshRendererUVE skips such entities.
struct MeshComponentUVE final {
    Asset::AssetGuidUVE meshGuid;
    Asset::AssetGuidUVE materialGuid;
    /// Which visibility layers this mesh belongs to, as a bitmask (layer N is bit 1<<N; the
    /// default 0x1 puts every mesh on layer 0, exactly where Godot's VisualInstance3D.layers
    /// starts). Anything that gates rendering by layer masks - the VisibilityRegion3D
    /// membership test, and any future camera cull mask - multiplies across this field with a
    /// single AND. A mesh on NO layers (0) is never managed by a region, which stays an
    /// authoring choice, not an error.
    std::uint32_t visibilityLayers = 0x00000001U;
};

/// Validates the authored asset-reference pair without resolving assets. Unassigned, mesh only
/// (drawn with the built-in lit shader until a material is chosen) and both are valid; a material
/// with no mesh is rejected at persistence/runtime boundaries, because it has nothing to draw on.
[[nodiscard]] bool IsMeshComponentValidUVE(const MeshComponentUVE& component) noexcept;

} // namespace UVE::Scene
