// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the MeshInstance3D scene object: the component set and defaults a
/// freshly created MeshInstance3D entity attaches — one empty MeshComponentUVE ready for an
/// author to bind a mesh/material asset pair onto.
struct MeshInstance3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "MeshInstance3D";

    MeshComponentUVE mesh{};
};

[[nodiscard]] bool IsMeshInstance3DObjectDefinitionValidUVE(const MeshInstance3DObjectDefinitionUVE& value) noexcept;

void ApplyMeshInstance3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const MeshInstance3DObjectDefinitionUVE& value);

/// Node policy for a `MeshInstance3D` object.
///
/// Centralises the asset-binding and surface-override predicates that `MeshRendererUVE` and
/// scene inspection query on every mesh candidate.
class MeshInstance3DUVE final {
public:
    MeshInstance3DUVE() = delete;

    /// True when `mesh` passes validation and references a non-zero mesh asset GUID.
    [[nodiscard]] static bool HasAssignedMeshUVE(const MeshComponentUVE& mesh) noexcept;

    /// True when `mesh` passes validation and references a non-zero material asset GUID.
    [[nodiscard]] static bool HasAssignedMaterialUVE(const MeshComponentUVE& mesh) noexcept;

    /// True when `surface` is present, valid, and names a non-empty `materialOverridePath`.
    [[nodiscard]] static bool HasSurfaceMaterialOverrideUVE(
        const SurfaceInstanceComponentUVE* surface) noexcept;

    /// Returns `surfaceOverrideGuid` when valid, otherwise falls back to `mesh.materialGuid`.
    [[nodiscard]] static Asset::AssetGuidUVE ResolveEffectiveMaterialGuidUVE(
        const MeshComponentUVE& mesh, Asset::AssetGuidUVE surfaceOverrideGuid) noexcept;

    /// True when both the resolved mesh GUID and effective material GUID are non-zero, so the
    /// instance can enter the asset-resolution pass.
    [[nodiscard]] static bool HasRenderableAssetPairUVE(
        Asset::AssetGuidUVE effectiveMeshGuid, Asset::AssetGuidUVE effectiveMaterialGuid) noexcept;

    /// True when this mesh instance participates in the requested pass (`shadowPass` checks
    /// shadow casting; colour pass checks view drawing).
    [[nodiscard]] static bool ParticipatesInPassUVE(
        const SurfaceInstanceComponentUVE* surface, bool shadowPass) noexcept;
};

} // namespace UVE::Scene
