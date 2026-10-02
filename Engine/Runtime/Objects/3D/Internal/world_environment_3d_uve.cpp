// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/world_environment_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsWorldEnvironment3DNodeComponentValidUVE(const WorldEnvironment3DNodeComponentUVE& value) noexcept {
    return IsBounded3DNodeStringUVE(value.skyAssetPath) && IsFinite3DNodeVectorUVE(value.ambientColor) &&
           IsFinite3DNodeVectorUVE(value.fogColor) && value.ambientColor.x >= 0.0F && value.ambientColor.y >= 0.0F &&
           value.ambientColor.z >= 0.0F && std::isfinite(value.ambientEnergy) && value.ambientEnergy >= 0.0F &&
           std::isfinite(value.exposure) && value.exposure > 0.0F && std::isfinite(value.fogDensity) &&
           value.fogDensity >= 0.0F;
}

void ApplyWorldEnvironmentNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                            const WorldEnvironmentNodeDefinitionUVE& value) {
    EnsureNodeBaselineUVE(entityManager, entity, WorldEnvironmentNodeDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<WorldEnvironment3DNodeComponentUVE>(entity)) {
        entityManager.AddComponentUVE<WorldEnvironment3DNodeComponentUVE>(entity, value.environment);
    }
}

} // namespace UVE::Scene
