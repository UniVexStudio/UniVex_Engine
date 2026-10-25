// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/sky_3d_uve.h"

#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsSky3DObjectComponentValidUVE(const Sky3DComponentUVE& value) noexcept {
    // Every source a saved document could name, including the reserved one: an authored scene that
    // selected it must still load, and the renderer falls back to the gradient for it.
    const bool sourceKnown = value.source == SkySourceUVE::Gradient || value.source == SkySourceUVE::Panorama ||
                             value.source == SkySourceUVE::Atmosphere;
    const auto colorIsSane = [](const Math::Vector3UVE& color) noexcept {
        return IsFinite3DObjectVectorUVE(color) && color.x >= 0.0F && color.y >= 0.0F && color.z >= 0.0F;
    };
    return sourceKnown && colorIsSane(value.skyColor) && colorIsSane(value.horizonColor) &&
           colorIsSane(value.groundColor) && std::isfinite(value.skyCurve) && value.skyCurve > 0.0F &&
           std::isfinite(value.groundCurve) && value.groundCurve > 0.0F &&
           IsBounded3DObjectStringUVE(value.panoramaPath) && std::isfinite(value.panoramaRotationDegrees) &&
           std::isfinite(value.panoramaExposure);
}

void ApplySky3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                   const Sky3DObjectDefinitionUVE& value) {
    EnsureObjectBaselineUVE(entityManager, entity, Sky3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<Sky3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Sky3DComponentUVE>(entity, value.sky);
    }
}

} // namespace UVE::Scene
