// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/directional_light_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] Math::Vector3UVE GizmoColorUVE(const Math::Vector3UVE& color, const float energy) noexcept {
    const float scale = std::max(energy, 0.35F);
    return Math::Vector3UVE{std::clamp(color.x * scale, 0.15F, 1.0F),
                            std::clamp(color.y * scale, 0.15F, 1.0F),
                            std::clamp(color.z * scale, 0.15F, 1.0F)};
}

void TryAddLightDirectionGizmoUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                  const Math::Vector3UVE& color, const float energy,
                                  std::vector<LightDirectionGizmoUVE>& out) {
    if (!entityManager.IsAliveUVE(entity) ||
        entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        return;
    }
    const WorldTransformComponentUVE& world =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
    Math::QuaternionUVE rotation{};
    if (!Math::IsFiniteUVE(world.worldPosition) || !Math::TryNormalizeUVE(world.worldRotation, rotation)) {
        return;
    }
    const Math::Vector3UVE direction = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    if (!Math::IsFiniteUVE(direction) || Math::LengthSquaredUVE(direction) < 1.0e-8F) {
        return;
    }
    LightDirectionGizmoUVE gizmo;
    gizmo.origin = world.worldPosition;
    gizmo.direction = Math::NormalizeUVE(direction);
    gizmo.color = GizmoColorUVE(color, energy);
    out.push_back(gizmo);
}

} // namespace

bool IsDirectionalLight3DComponentValidUVE(const DirectionalLight3DComponentUVE& value) noexcept {
    return std::isfinite(value.shadowMaxDistance) && value.shadowMaxDistance >= 0.0F &&
           std::isfinite(value.shadowSplitBlend) && value.shadowSplitBlend >= -1.0F && value.shadowSplitBlend <= 1.0F &&
           std::isfinite(value.shadowDistanceFadeRange) && value.shadowDistanceFadeRange >= 0.0F;
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

void CollectLightDirectionGizmosUVE(IEntityManagerUVE& entityManager, std::vector<LightDirectionGizmoUVE>& out) {
    entityManager.ForEachUVE<WorldTransformComponentUVE, DirectionalLight3DComponentUVE, LightEmitterComponentUVE>(
        [&entityManager, &out](const EntityUVE entity, const WorldTransformComponentUVE&,
                               const DirectionalLight3DComponentUVE& directional,
                               const LightEmitterComponentUVE& emitter) {
            if (!IsDirectionalLight3DComponentValidUVE(directional) ||
                !IsLightEmitterComponentValidUVE(emitter)) {
                return;
            }
            TryAddLightDirectionGizmoUVE(entityManager, entity, emitter.color, emitter.energy, out);
        });
    entityManager.ForEachUVE<WorldTransformComponentUVE, LightComponentUVE>(
        [&entityManager, &out](const EntityUVE entity, const WorldTransformComponentUVE&,
                               const LightComponentUVE& light) {
            if (!IsLightComponentValidUVE(light) || light.type != LightTypeUVE::Directional ||
                entityManager.HasComponentUVE<DirectionalLight3DComponentUVE>(entity)) {
                return;
            }
            TryAddLightDirectionGizmoUVE(entityManager, entity, light.color, light.intensity, out);
        });
}

} // namespace UVE::Scene
