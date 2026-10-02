// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/directional_light_3d_uve.h"

#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsDirectionalLight3DComponentValidUVE(const DirectionalLight3DComponentUVE& value) noexcept {
    return std::isfinite(value.shadowMaxDistance) && value.shadowMaxDistance >= 0.0F &&
           std::isfinite(value.shadowSplitBlend) && value.shadowSplitBlend >= 0.0F && value.shadowSplitBlend <= 1.0F;
}

void ApplyDirectionalLight3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                              const DirectionalLight3DObjectDefinitionUVE& value) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // The definition's emitter first, so the base (which only adds what is missing) keeps it.
    if (!entityManager.HasComponentUVE<LightEmitterComponentUVE>(entity)) {
        entityManager.AddComponentUVE<LightEmitterComponentUVE>(entity, value.emitter);
    }
    ApplyLightEmitter3DBaseUVE(entityManager, entity, DirectionalLight3DObjectDefinitionUVE::defaultName);
    if (!entityManager.HasComponentUVE<DirectionalLight3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<DirectionalLight3DComponentUVE>(entity, value.light);
    }
}

} // namespace UVE::Scene
