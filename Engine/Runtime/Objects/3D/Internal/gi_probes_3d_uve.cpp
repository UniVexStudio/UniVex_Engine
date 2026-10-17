// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/gi_probes_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

void ApplyVoxelGI3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const VoxelGI3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, VoxelGI3DObjectDefinitionUVE::defaultName);
    VoxelGI3DComponentUVE out = value.voxelGi;
    if (!IsFinite3DObjectVectorUVE(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F || out.halfExtents.z <= 0.0F) {
        out.halfExtents = Math::Vector3UVE{10.0F, 10.0F, 10.0F};
    }
    out.subdivisions = std::clamp(out.subdivisions, 16U, 512U);
    if (!std::isfinite(out.energy) || out.energy < 0.0F) {
        out.energy = 1.0F;
    }
    entityManager.AddComponentUVE<VoxelGI3DComponentUVE>(entity, out);
}

void ApplyLightmapGI3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const LightmapGI3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, LightmapGI3DObjectDefinitionUVE::defaultName);
    LightmapGI3DComponentUVE out = value.lightmapGi;
    if (!IsFinite3DObjectVectorUVE(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F || out.halfExtents.z <= 0.0F) {
        out.halfExtents = Math::Vector3UVE{20.0F, 10.0F, 20.0F};
    }
    if (!std::isfinite(out.energy) || out.energy < 0.0F) {
        out.energy = 1.0F;
    }
    entityManager.AddComponentUVE<LightmapGI3DComponentUVE>(entity, out);
}

void ApplyLightmapProbe3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                             const EntityUVE entity,
                                             const LightmapProbe3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              LightmapProbe3DObjectDefinitionUVE::defaultName);
    LightmapProbe3DComponentUVE out = value.probe;
    if (!std::isfinite(out.influenceRadius) || out.influenceRadius <= 0.0F) {
        out.influenceRadius = 5.0F;
    }
    entityManager.AddComponentUVE<LightmapProbe3DComponentUVE>(entity, out);
}

Math::ColorUVE EvaluateIndirectGI3DIrradianceAtPointUVE(IEntityManagerUVE& entityManager,
                                                        const Math::Vector3UVE& worldPosition) {
    if (!IsFinite3DObjectVectorUVE(worldPosition)) {
        return Math::ColorUVE{0.0F, 0.0F, 0.0F};
    }
    Math::ColorUVE accum{0.0F, 0.0F, 0.0F};
    entityManager.ForEachUVE<VoxelGI3DComponentUVE, WorldTransformComponentUVE>(
        [&](const EntityUVE entity, const VoxelGI3DComponentUVE& vgi,
            const WorldTransformComponentUVE& world) {
            if (!vgi.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsWorldTransformComponentValidUVE(world)) {
                return;
            }
            const Math::Vector3UVE d{std::fabs(worldPosition.x - world.worldPosition.x),
                                     std::fabs(worldPosition.y - world.worldPosition.y),
                                     std::fabs(worldPosition.z - world.worldPosition.z)};
            if (d.x <= vgi.halfExtents.x && d.y <= vgi.halfExtents.y && d.z <= vgi.halfExtents.z) {
                accum.r += vgi.ambientTint.r * vgi.energy;
                accum.g += vgi.ambientTint.g * vgi.energy;
                accum.b += vgi.ambientTint.b * vgi.energy;
            }
        });
    entityManager.ForEachUVE<LightmapGI3DComponentUVE, WorldTransformComponentUVE>(
        [&](const EntityUVE entity, const LightmapGI3DComponentUVE& lm,
            const WorldTransformComponentUVE& world) {
            if (!lm.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsWorldTransformComponentValidUVE(world)) {
                return;
            }
            const Math::Vector3UVE d{std::fabs(worldPosition.x - world.worldPosition.x),
                                     std::fabs(worldPosition.y - world.worldPosition.y),
                                     std::fabs(worldPosition.z - world.worldPosition.z)};
            if (d.x <= lm.halfExtents.x && d.y <= lm.halfExtents.y && d.z <= lm.halfExtents.z) {
                accum.r += lm.ambientIrradiance.r * lm.energy;
                accum.g += lm.ambientIrradiance.g * lm.energy;
                accum.b += lm.ambientIrradiance.b * lm.energy;
            }
        });
    entityManager.ForEachUVE<LightmapProbe3DComponentUVE, WorldTransformComponentUVE>(
        [&](const EntityUVE entity, const LightmapProbe3DComponentUVE& probe,
            const WorldTransformComponentUVE& world) {
            if (!probe.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsWorldTransformComponentValidUVE(world) || probe.influenceRadius <= 0.0F) {
                return;
            }
            const float dist =
                std::sqrt(Math::LengthSquaredUVE(worldPosition - world.worldPosition));
            if (dist < probe.influenceRadius) {
                const float w = 1.0F - (dist / probe.influenceRadius);
                accum.r += probe.sampledIrradiance.r * w;
                accum.g += probe.sampledIrradiance.g * w;
                accum.b += probe.sampledIrradiance.b * w;
            }
        });
    return accum;
}

} // namespace UVE::Scene
