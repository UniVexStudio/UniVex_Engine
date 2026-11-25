// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "directional_light_3d.h"

#include <algorithm>
#include <cmath>

#include "editor_internal_entity_component.h"
#include "light_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "color.h"
#include "quaternion.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] Math::Vector3 GizmoColor(const Math::Vector3& color, const float energy) noexcept {
    const float scale = std::max(energy, 0.35F);
    return Math::Vector3{std::clamp(color.x * scale, 0.15F, 1.0F),
                            std::clamp(color.y * scale, 0.15F, 1.0F),
                            std::clamp(color.z * scale, 0.15F, 1.0F)};
}

void TryAddLightDirectionGizmo(IEntityManager& entityManager, const Entity entity,
                                  const Math::Vector3& color, const float energy,
                                  std::vector<LightDirectionGizmo>& out) {
    if (!entityManager.IsAlive(entity) ||
        entityManager.HasComponent<EditorInternalEntityComponent>(entity) ||
        !entityManager.HasComponent<WorldTransformComponent>(entity)) {
        return;
    }
    const WorldTransformComponent& world =
        entityManager.GetComponent<WorldTransformComponent>(entity);
    Math::Quaternion rotation{};
    if (!Math::IsFinite(world.worldPosition) || !Math::TryNormalize(world.worldRotation, rotation)) {
        return;
    }
    const Math::Vector3 direction = Math::RotateVector(rotation, Math::Vector3{0.0F, 0.0F, -1.0F});
    if (!Math::IsFinite(direction) || Math::LengthSquared(direction) < 1.0e-8F) {
        return;
    }
    LightDirectionGizmo gizmo;
    gizmo.origin = world.worldPosition;
    gizmo.direction = Math::Normalize(direction);
    gizmo.color = GizmoColor(color, energy);
    out.push_back(gizmo);
}

} // namespace

bool IsDirectionalLight3DComponentValid(const DirectionalLight3DComponent& value) noexcept {
    return std::isfinite(value.shadowMaxDistance) && value.shadowMaxDistance >= 0.0F &&
           std::isfinite(value.shadowSplitBlend) && value.shadowSplitBlend >= -1.0F && value.shadowSplitBlend <= 1.0F &&
           std::isfinite(value.shadowDistanceFadeRange) && value.shadowDistanceFadeRange >= 0.0F &&
           // Upper bounds on the disc as well as lower ones: a diameter near 180 degrees would put
           // the disc's rim at the horizon in every direction, and a limb figure above 1 would make
           // the rim negative. Neither is authorable, so neither is loadable.
           std::isfinite(value.discAngularDiameterDegrees) && value.discAngularDiameterDegrees >= 0.0F &&
           value.discAngularDiameterDegrees <= 90.0F && std::isfinite(value.discIntensity) &&
           value.discIntensity >= 0.0F && std::isfinite(value.discLimbDarkening) &&
           value.discLimbDarkening >= 0.0F && value.discLimbDarkening <= 1.0F;
}

void ApplyDirectionalLight3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                              const DirectionalLight3DObjectDefinition& value) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    // The definition's emitter first, so the base (which only adds what is missing) keeps it.
    if (!entityManager.HasComponent<LightEmitterComponent>(entity)) {
        entityManager.AddComponent<LightEmitterComponent>(entity, value.emitter);
    }
    ApplyLightEmitter3DBase(entityManager, entity, DirectionalLight3DObjectDefinition::defaultName);
    if (!entityManager.HasComponent<DirectionalLight3DComponent>(entity)) {
        entityManager.AddComponent<DirectionalLight3DComponent>(entity, value.light);
    }
}

void CollectLightDirectionGizmos(IEntityManager& entityManager, std::vector<LightDirectionGizmo>& out) {
    entityManager.ForEach<WorldTransformComponent, DirectionalLight3DComponent, LightEmitterComponent>(
        [&entityManager, &out](const Entity entity, const WorldTransformComponent&,
                               const DirectionalLight3DComponent& directional,
                               const LightEmitterComponent& emitter) {
            if (!IsDirectionalLight3DComponentValid(directional) ||
                !IsLightEmitterComponentValid(emitter)) {
                return;
            }
            TryAddLightDirectionGizmo(entityManager, entity, Math::ToVector3(emitter.color),
                                            emitter.energy, out);
        });
    entityManager.ForEach<WorldTransformComponent, LightComponent>(
        [&entityManager, &out](const Entity entity, const WorldTransformComponent&,
                               const LightComponent& light) {
            if (!IsLightComponentValid(light) || light.type != LightType::Directional ||
                entityManager.HasComponent<DirectionalLight3DComponent>(entity)) {
                return;
            }
            TryAddLightDirectionGizmo(entityManager, entity, Math::ToVector3(light.color),
                                            light.intensity, out);
        });
}

} // namespace UVE::Scene
