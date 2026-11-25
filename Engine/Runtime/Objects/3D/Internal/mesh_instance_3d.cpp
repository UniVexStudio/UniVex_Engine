// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "mesh_instance_3d.h"

#include <algorithm>
#include <string>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

bool IsMeshInstance3DObjectDefinitionValid(const MeshInstance3DObjectDefinition& value) noexcept {
    return IsMeshComponentValid(value.mesh);
}

void ApplyMeshInstance3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                            const MeshInstance3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity, MeshInstance3DObjectDefinition::defaultName);
    entityManager.AddComponent<MeshComponent>(entity, value.mesh);
}

bool MeshInstance3D::HasAssignedMesh(const MeshComponent& mesh) noexcept {
    return IsMeshComponentValid(mesh) && HasAssignedMeshAsset(mesh);
}

bool MeshInstance3D::HasAssignedMaterial(const MeshComponent& mesh) noexcept {
    return IsMeshComponentValid(mesh) && HasAssignedMaterialAsset(mesh);
}

bool MeshInstance3D::HasSurfaceMaterialOverride(
    const SurfaceInstanceComponent* const surface) noexcept {
    return surface != nullptr && IsSurfaceInstanceComponentValid(*surface) &&
           !surface->materialOverridePath.empty();
}

std::string_view MeshInstance3D::SurfaceMaterialOverridePath(
    const SurfaceInstanceComponent& surface, const std::size_t surfaceIndex) noexcept {
    if (!surface.materialOverridePath.empty() || surfaceIndex >= surface.surfaceMaterialOverridePaths.size()) {
        return {};
    }
    return surface.surfaceMaterialOverridePaths[surfaceIndex];
}

bool MeshInstance3D::HasPerSurfaceMaterialOverride(
    const SurfaceInstanceComponent* const surface) noexcept {
    return surface != nullptr && IsSurfaceInstanceComponentValid(*surface) &&
           surface->materialOverridePath.empty() &&
           std::any_of(surface->surfaceMaterialOverridePaths.cbegin(),
                       surface->surfaceMaterialOverridePaths.cend(),
                       [](const std::string& path) { return !path.empty(); });
}

Asset::AssetGuid MeshInstance3D::ResolveEffectiveMaterialGuid(
    const MeshComponent& mesh, const Asset::AssetGuid surfaceOverrideGuid) noexcept {
    if (surfaceOverrideGuid != Asset::kInvalidAssetGuid) {
        return surfaceOverrideGuid;
    }
    return mesh.materialGuid;
}

bool MeshInstance3D::HasRenderableAssetPair(
    const Asset::AssetGuid effectiveMeshGuid,
    const Asset::AssetGuid effectiveMaterialGuid) noexcept {
    return effectiveMeshGuid != Asset::kInvalidAssetGuid &&
           effectiveMaterialGuid != Asset::kInvalidAssetGuid;
}

bool MeshInstance3D::ParticipatesInPass(
    const SurfaceInstanceComponent* const surface, const bool shadowPass) noexcept {
    if (surface == nullptr) {
        return true;
    }
    if (!IsSurfaceInstanceComponentValid(*surface)) {
        return false;
    }
    return shadowPass ? SurfaceInstance3DCastsShadow(*surface)
                      : SurfaceInstance3DDrawsInView(*surface);
}

} // namespace UVE::Scene
