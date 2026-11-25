// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gi_probes_3d.h"

#include <algorithm>
#include <cmath>

#include "disabled_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

void ApplyVoxelGI3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                       const VoxelGI3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, VoxelGI3DObjectDefinition::defaultName);
    VoxelGI3DComponent out = value.voxelGi;
    if (!IsFinite3DObjectVector(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F || out.halfExtents.z <= 0.0F) {
        out.halfExtents = Math::Vector3{10.0F, 10.0F, 10.0F};
    }
    out.subdivisions = std::clamp(out.subdivisions, 16U, 512U);
    if (!std::isfinite(out.energy) || out.energy < 0.0F) {
        out.energy = 1.0F;
    }
    entityManager.AddComponent<VoxelGI3DComponent>(entity, out);
}

void ApplyLightmapGI3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                          const LightmapGI3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, LightmapGI3DObjectDefinition::defaultName);
    LightmapGI3DComponent out = value.lightmapGi;
    if (!IsFinite3DObjectVector(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F || out.halfExtents.z <= 0.0F) {
        out.halfExtents = Math::Vector3{20.0F, 10.0F, 20.0F};
    }
    if (!std::isfinite(out.energy) || out.energy < 0.0F) {
        out.energy = 1.0F;
    }
    entityManager.AddComponent<LightmapGI3DComponent>(entity, out);
}

void ApplyLightmapProbe3DObjectDefinition(IEntityManager& entityManager,
                                             const Entity entity,
                                             const LightmapProbe3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              LightmapProbe3DObjectDefinition::defaultName);
    LightmapProbe3DComponent out = value.probe;
    if (!std::isfinite(out.influenceRadius) || out.influenceRadius <= 0.0F) {
        out.influenceRadius = 5.0F;
    }
    entityManager.AddComponent<LightmapProbe3DComponent>(entity, out);
}

Math::Color EvaluateIndirectGI3DIrradianceAtPoint(IEntityManager& entityManager,
                                                        const Math::Vector3& worldPosition) {
    if (!IsFinite3DObjectVector(worldPosition)) {
        return Math::Color{0.0F, 0.0F, 0.0F};
    }
    Math::Color accum{0.0F, 0.0F, 0.0F};
    entityManager.ForEach<VoxelGI3DComponent, WorldTransformComponent>(
        [&](const Entity entity, const VoxelGI3DComponent& vgi,
            const WorldTransformComponent& world) {
            if (!vgi.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsWorldTransformComponentValid(world)) {
                return;
            }
            const Math::Vector3 d{std::fabs(worldPosition.x - world.worldPosition.x),
                                     std::fabs(worldPosition.y - world.worldPosition.y),
                                     std::fabs(worldPosition.z - world.worldPosition.z)};
            if (d.x <= vgi.halfExtents.x && d.y <= vgi.halfExtents.y && d.z <= vgi.halfExtents.z) {
                accum.r += vgi.ambientTint.r * vgi.energy;
                accum.g += vgi.ambientTint.g * vgi.energy;
                accum.b += vgi.ambientTint.b * vgi.energy;
            }
        });
    entityManager.ForEach<LightmapGI3DComponent, WorldTransformComponent>(
        [&](const Entity entity, const LightmapGI3DComponent& lm,
            const WorldTransformComponent& world) {
            if (!lm.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsWorldTransformComponentValid(world)) {
                return;
            }
            const Math::Vector3 d{std::fabs(worldPosition.x - world.worldPosition.x),
                                     std::fabs(worldPosition.y - world.worldPosition.y),
                                     std::fabs(worldPosition.z - world.worldPosition.z)};
            if (d.x <= lm.halfExtents.x && d.y <= lm.halfExtents.y && d.z <= lm.halfExtents.z) {
                accum.r += lm.ambientIrradiance.r * lm.energy;
                accum.g += lm.ambientIrradiance.g * lm.energy;
                accum.b += lm.ambientIrradiance.b * lm.energy;
            }
        });
    entityManager.ForEach<LightmapProbe3DComponent, WorldTransformComponent>(
        [&](const Entity entity, const LightmapProbe3DComponent& probe,
            const WorldTransformComponent& world) {
            if (!probe.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsWorldTransformComponentValid(world) || probe.influenceRadius <= 0.0F) {
                return;
            }
            const float dist =
                std::sqrt(Math::LengthSquared(worldPosition - world.worldPosition));
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
