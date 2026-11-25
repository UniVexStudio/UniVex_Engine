// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ray_cast_3d_step.h"

#include <optional>

#include "world_transform_component.h"
#include "raycast_query.h"

namespace UVE::Physics {
namespace {

[[nodiscard]] RayCast3DStepResult Cleared(Scene::RayCast3DComponent& rayCast,
                                                const RayCast3DStepCode code) {
    Scene::RayCast3D::ClearResult(rayCast);
    RayCast3DStepResult result;
    result.code = code;
    return result;
}

} // namespace

RayCast3DStepResult StepRayCast3D(Scene::IEntityManager& entityManager,
                                         const IRaycastSystem& raycastSystem,
                                         const Scene::Entity entity) {
    if (!entityManager.IsAlive(entity)) {
        RayCast3DStepResult result;
        result.code = RayCast3DStepCode::UnknownEntity;
        return result;
    }
    if (!entityManager.HasComponent<Scene::RayCast3DComponent>(entity)) {
        RayCast3DStepResult result;
        result.code = RayCast3DStepCode::NotARayCast;
        return result;
    }

    Scene::RayCast3DComponent& rayCast = entityManager.GetComponent<Scene::RayCast3DComponent>(entity);
    if (!Scene::IsRayCast3DObjectComponentValid(rayCast)) {
        return Cleared(rayCast, RayCast3DStepCode::InvalidComponent);
    }
    if (!Scene::RayCast3D::IsCasting(rayCast)) {
        return Cleared(rayCast, RayCast3DStepCode::Disabled);
    }
    if (!entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
        return Cleared(rayCast, RayCast3DStepCode::MissingTransform);
    }

    const Scene::WorldTransformComponent& worldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    RaycastQuery query{};
    query.ray.origin = worldTransform.worldPosition;
    query.ray.direction =
        Scene::RayCast3D::ResolveWorldDirection(rayCast.direction, worldTransform.worldRotation);
    query.maxDistance = rayCast.length;
    query.layerMask = rayCast.collisionMask;
    query.ignoreEntity = entity;
    query.excludedEntities = Scene::RayCast3D::ExclusionSpan(rayCast);

    const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, query);
    if (!hit.has_value()) {
        return Cleared(rayCast, RayCast3DStepCode::Miss);
    }

    Scene::RayCast3D::RecordHit(rayCast, hit->entity, hit->point, hit->normal);
    RayCast3DStepResult result;
    result.code = RayCast3DStepCode::Hit;
    result.hasHit = true;
    result.hitEntity = hit->entity;
    result.hitPosition = hit->point;
    result.hitNormal = hit->normal;
    return result;
}

} // namespace UVE::Physics
