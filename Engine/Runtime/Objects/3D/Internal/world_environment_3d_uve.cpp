// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/world_environment_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsWorldEnvironment3DObjectComponentValidUVE(const WorldEnvironment3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.skyAssetPath) && IsFinite3DObjectVectorUVE(value.ambientColor) &&
           IsFinite3DObjectVectorUVE(value.fogColor) && value.ambientColor.x >= 0.0F && value.ambientColor.y >= 0.0F &&
           value.ambientColor.z >= 0.0F && std::isfinite(value.ambientEnergy) && value.ambientEnergy >= 0.0F &&
           std::isfinite(value.exposure) && value.exposure > 0.0F && std::isfinite(value.fogDensity) &&
           value.fogDensity >= 0.0F;
}

void ApplyWorldEnvironmentObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                            const WorldEnvironmentObjectDefinitionUVE& value) {
    EnsureObjectBaselineUVE(entityManager, entity, WorldEnvironmentObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<WorldEnvironment3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(entity, value.environment);
    }
}

} // namespace UVE::Scene
