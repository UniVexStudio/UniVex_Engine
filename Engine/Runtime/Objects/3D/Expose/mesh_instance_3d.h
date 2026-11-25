// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "asset_guid.h"
#include "entity.h"
#include "mesh_component.h"
#include "surface_instance_component.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the MeshInstance3D scene object: the component set and defaults a
/// freshly created MeshInstance3D entity attaches — one empty MeshComponent ready for an
/// author to bind a mesh/material asset pair onto.
struct MeshInstance3DObjectDefinition final {
    static constexpr std::string_view defaultName = "MeshInstance3D";

    MeshComponent mesh{};
};

[[nodiscard]] bool IsMeshInstance3DObjectDefinitionValid(const MeshInstance3DObjectDefinition& value) noexcept;

void ApplyMeshInstance3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const MeshInstance3DObjectDefinition& value);

/// Node policy for a `MeshInstance3D` object.
///
/// Centralises the asset-binding and surface-override predicates that `MeshRenderer` and
/// scene inspection query on every mesh candidate.
class MeshInstance3D final {
public:
    MeshInstance3D() = delete;

    /// True when `mesh` passes validation and references a non-zero mesh asset GUID.
    [[nodiscard]] static bool HasAssignedMesh(const MeshComponent& mesh) noexcept;

    /// True when `mesh` passes validation and references a non-zero material asset GUID.
    [[nodiscard]] static bool HasAssignedMaterial(const MeshComponent& mesh) noexcept;

    /// True when `surface` is present, valid, and names a non-empty `materialOverridePath`.
    /// That one path replaces the material of EVERY surface, so it outranks the per-surface list.
    [[nodiscard]] static bool HasSurfaceMaterialOverride(
        const SurfaceInstanceComponent* surface) noexcept;

    /// The override authored for one surface: the entry at `surfaceIndex`, or an empty view when
    /// the list is shorter. Empty means the surface keeps its own material. Returns empty for
    /// every surface while a whole-object override is set, because that one already covers them -
    /// asking per surface would then be asking a question the object has answered for itself.
    [[nodiscard]] static std::string_view SurfaceMaterialOverridePath(
        const SurfaceInstanceComponent& surface, std::size_t surfaceIndex) noexcept;

    /// True when `surface` overrides at least one surface individually and no whole-object
    /// override outranks the list. That combination is what turns a mesh with a single implicit
    /// surface into a per-surface draw, and what keeps a mesh with no material of its own drawable.
    [[nodiscard]] static bool HasPerSurfaceMaterialOverride(
        const SurfaceInstanceComponent* surface) noexcept;

    /// Returns `surfaceOverrideGuid` when valid, otherwise falls back to `mesh.materialGuid`.
    [[nodiscard]] static Asset::AssetGuid ResolveEffectiveMaterialGuid(
        const MeshComponent& mesh, Asset::AssetGuid surfaceOverrideGuid) noexcept;

    /// True when both the resolved mesh GUID and effective material GUID are non-zero, so the
    /// instance can enter the asset-resolution pass.
    [[nodiscard]] static bool HasRenderableAssetPair(
        Asset::AssetGuid effectiveMeshGuid, Asset::AssetGuid effectiveMaterialGuid) noexcept;

    /// True when this mesh instance participates in the requested pass (`shadowPass` checks
    /// shadow casting; colour pass checks view drawing).
    [[nodiscard]] static bool ParticipatesInPass(
        const SurfaceInstanceComponent* surface, bool shadowPass) noexcept;
};

} // namespace UVE::Scene
