// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/mesh_instance_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsMeshInstance3DObjectDefinitionValidUVE(const MeshInstance3DObjectDefinitionUVE& value) noexcept {
    return IsMeshComponentValidUVE(value.mesh);
}

void ApplyMeshInstance3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                            const MeshInstance3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, MeshInstance3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<MeshComponentUVE>(entity, value.mesh);
}

bool MeshInstance3DUVE::HasAssignedMeshUVE(const MeshComponentUVE& mesh) noexcept {
    return IsMeshComponentValidUVE(mesh) && HasAssignedMeshAssetUVE(mesh);
}

bool MeshInstance3DUVE::HasAssignedMaterialUVE(const MeshComponentUVE& mesh) noexcept {
    return IsMeshComponentValidUVE(mesh) && HasAssignedMaterialAssetUVE(mesh);
}

bool MeshInstance3DUVE::HasSurfaceMaterialOverrideUVE(
    const SurfaceInstanceComponentUVE* const surface) noexcept {
    return surface != nullptr && IsSurfaceInstanceComponentValidUVE(*surface) &&
           !surface->materialOverridePath.empty();
}

Asset::AssetGuidUVE MeshInstance3DUVE::ResolveEffectiveMaterialGuidUVE(
    const MeshComponentUVE& mesh, const Asset::AssetGuidUVE surfaceOverrideGuid) noexcept {
    if (surfaceOverrideGuid != Asset::kInvalidAssetGuidUVE) {
        return surfaceOverrideGuid;
    }
    return mesh.materialGuid;
}

bool MeshInstance3DUVE::HasRenderableAssetPairUVE(
    const Asset::AssetGuidUVE effectiveMeshGuid,
    const Asset::AssetGuidUVE effectiveMaterialGuid) noexcept {
    return effectiveMeshGuid != Asset::kInvalidAssetGuidUVE &&
           effectiveMaterialGuid != Asset::kInvalidAssetGuidUVE;
}

bool MeshInstance3DUVE::ParticipatesInPassUVE(
    const SurfaceInstanceComponentUVE* const surface, const bool shadowPass) noexcept {
    if (surface == nullptr) {
        return true;
    }
    if (!IsSurfaceInstanceComponentValidUVE(*surface)) {
        return false;
    }
    return shadowPass ? SurfaceInstance3DCastsShadowUVE(*surface)
                      : SurfaceInstance3DDrawsInViewUVE(*surface);
}

} // namespace UVE::Scene
