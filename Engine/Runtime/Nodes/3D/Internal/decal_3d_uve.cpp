// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/decal_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/abstract_nodes_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsUnitIntervalUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsDecal3DNodeComponentValidUVE(const Decal3DNodeComponentUVE& value) noexcept {
    return IsBounded3DNodeStringUVE(value.materialAssetPath) && IsFinite3DNodeVectorUVE(value.size) &&
           value.size.x > 0.0F && value.size.y > 0.0F && value.size.z > 0.0F && IsNonNegativeUVE(value.lifetime) &&
           value.projection <= DecalProjectionModeUVE::Cylinder && IsNonNegativeUVE(value.modulate.x) &&
           IsNonNegativeUVE(value.modulate.y) && IsNonNegativeUVE(value.modulate.z) &&
           IsNonNegativeUVE(value.emissionEnergy) && IsUnitIntervalUVE(value.albedoMix) &&
           IsUnitIntervalUVE(value.normalFade) && IsNonNegativeUVE(value.upperFade) &&
           IsNonNegativeUVE(value.lowerFade) && IsNonNegativeUVE(value.distanceFadeBegin) &&
           IsNonNegativeUVE(value.distanceFadeLength);
}

void ApplyDecal3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                   const Decal3DNodeDefinitionUVE& value) {
    ApplyRenderInstance3DBaseUVE(entityManager, entity, Decal3DNodeDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<Decal3DNodeComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Decal3DNodeComponentUVE>(entity, value.decal);
    }
}

} // namespace UVE::Scene
