// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/ray_cast_3d_step_uve.h"

#include <optional>

#include "uve/component/world_transform_component_uve.h"
#include "uve/physics/raycast_query_uve.h"

namespace UVE::Physics {
namespace {

[[nodiscard]] RayCast3DStepResultUVE ClearedUVE(Scene::RayCast3DComponentUVE& rayCast,
                                                const RayCast3DStepCodeUVE code) {
    Scene::RayCast3DUVE::ClearResultUVE(rayCast);
    RayCast3DStepResultUVE result;
    result.code = code;
    return result;
}

} // namespace

RayCast3DStepResultUVE StepRayCast3DUVE(Scene::IEntityManagerUVE& entityManager,
                                         const IRaycastSystemUVE& raycastSystem,
                                         const Scene::EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity)) {
        RayCast3DStepResultUVE result;
        result.code = RayCast3DStepCodeUVE::UnknownEntity;
        return result;
    }
    if (!entityManager.HasComponentUVE<Scene::RayCast3DComponentUVE>(entity)) {
        RayCast3DStepResultUVE result;
        result.code = RayCast3DStepCodeUVE::NotARayCast;
        return result;
    }

    Scene::RayCast3DComponentUVE& rayCast = entityManager.GetComponentUVE<Scene::RayCast3DComponentUVE>(entity);
    if (!Scene::IsRayCast3DObjectComponentValidUVE(rayCast)) {
        return ClearedUVE(rayCast, RayCast3DStepCodeUVE::InvalidComponent);
    }
    if (!Scene::RayCast3DUVE::IsCastingUVE(rayCast)) {
        return ClearedUVE(rayCast, RayCast3DStepCodeUVE::Disabled);
    }
    if (!entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        return ClearedUVE(rayCast, RayCast3DStepCodeUVE::MissingTransform);
    }

    const Scene::WorldTransformComponentUVE& worldTransform =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    RaycastQueryUVE query{};
    query.ray.origin = worldTransform.worldPosition;
    query.ray.direction =
        Scene::RayCast3DUVE::ResolveWorldDirectionUVE(rayCast.direction, worldTransform.worldRotation);
    query.maxDistance = rayCast.length;
    query.layerMask = rayCast.collisionMask;
    query.ignoreEntity = entity;
    query.excludedEntities = Scene::RayCast3DUVE::ExclusionSpanUVE(rayCast);

    const std::optional<RaycastHitUVE> hit = raycastSystem.RaycastUVE(entityManager, query);
    if (!hit.has_value()) {
        return ClearedUVE(rayCast, RayCast3DStepCodeUVE::Miss);
    }

    Scene::RayCast3DUVE::RecordHitUVE(rayCast, hit->entity, hit->point, hit->normal);
    RayCast3DStepResultUVE result;
    result.code = RayCast3DStepCodeUVE::Hit;
    result.hasHit = true;
    result.hitEntity = hit->entity;
    result.hitPosition = hit->point;
    result.hitNormal = hit->normal;
    return result;
}

} // namespace UVE::Physics
