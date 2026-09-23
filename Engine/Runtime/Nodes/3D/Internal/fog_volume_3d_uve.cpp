// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/fog_volume_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/abstract_nodes_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsFogVolume3DNodeComponentValidUVE(const FogVolume3DNodeComponentUVE& value) noexcept {
    return value.shape <= FogVolumeShapeUVE::World && IsFinite3DNodeVectorUVE(value.size) && value.size.x > 0.0F &&
           value.size.y > 0.0F && value.size.z > 0.0F && std::isfinite(value.density) &&
           IsNonNegativeUVE(value.albedo.x) && IsNonNegativeUVE(value.albedo.y) && IsNonNegativeUVE(value.albedo.z) &&
           IsNonNegativeUVE(value.emission.x) && IsNonNegativeUVE(value.emission.y) &&
           IsNonNegativeUVE(value.emission.z) && IsNonNegativeUVE(value.heightFalloff) &&
           std::isfinite(value.edgeFade) && value.edgeFade >= 0.0F && value.edgeFade <= 1.0F &&
           IsBounded3DNodeStringUVE(value.materialAssetPath);
}

void ApplyFogVolume3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const FogVolume3DNodeDefinitionUVE& value) {
    ApplyRenderInstance3DBaseUVE(entityManager, entity, FogVolume3DNodeDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<FogVolume3DNodeComponentUVE>(entity)) {
        entityManager.AddComponentUVE<FogVolume3DNodeComponentUVE>(entity, value.fog);
    }
}

} // namespace UVE::Scene
