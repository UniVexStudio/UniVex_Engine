// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/shape_cast_3d_uve.h"

#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"
#include "uve/objects/3d/ray_cast_3d_uve.h"
#include "uve/physics/shape_cast_system_uve.h"

namespace UVE::Scene {
namespace {

void ClearShapeCast3DResultUVE(ShapeCast3DComponentUVE& cast) noexcept {
    cast.hit = false;
    cast.hitEntity = kInvalidEntityUVE;
    cast.hitCenter = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    cast.hitNormal = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    cast.hitDistance = 0.0F;
}

} // namespace

bool IsShapeCast3DComponentValidUVE(const ShapeCast3DComponentUVE& value) noexcept {
    if (!IsFinite3DObjectVectorUVE(value.direction) ||
        Math::LengthSquaredUVE(value.direction) <= 1.0e-8F || !std::isfinite(value.maxDistance) ||
        value.maxDistance <= 0.0F) {
        return false;
    }
    switch (value.shapeKind) {
    case ShapeCast3DShapeKindUVE::Sphere:
        return std::isfinite(value.radius) && value.radius > 0.0F;
    case ShapeCast3DShapeKindUVE::Box:
        return IsFinite3DObjectVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
               value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F;
    case ShapeCast3DShapeKindUVE::Capsule:
        return std::isfinite(value.radius) && value.radius > 0.0F && std::isfinite(value.height) &&
               value.height > 0.0F;
    }
    return false;
}

ShapeCast3DComponentUVE SanitizeShapeCast3DComponentUVE(
    const ShapeCast3DComponentUVE& value) noexcept {
    ShapeCast3DComponentUVE out = value;
    if (static_cast<std::uint8_t>(out.shapeKind) >
        static_cast<std::uint8_t>(ShapeCast3DShapeKindUVE::Capsule)) {
        out.shapeKind = ShapeCast3DShapeKindUVE::Sphere;
    }
    if (!IsFinite3DObjectVectorUVE(out.direction) ||
        Math::LengthSquaredUVE(out.direction) <= 1.0e-8F) {
        out.direction = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    }
    if (!std::isfinite(out.maxDistance) || out.maxDistance <= 0.0F) {
        out.maxDistance = 10.0F;
    }
    if (!std::isfinite(out.radius) || out.radius <= 0.0F) {
        out.radius = 0.5F;
    }
    if (!IsFinite3DObjectVectorUVE(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F || out.halfExtents.z <= 0.0F) {
        out.halfExtents = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    }
    if (!std::isfinite(out.height) || out.height <= 0.0F) {
        out.height = 1.5F;
    }
    return out;
}

bool IsShapeCast3DObjectDefinitionValidUVE(
    const ShapeCast3DObjectDefinitionUVE& value) noexcept {
    return IsShapeCast3DComponentValidUVE(value.shapeCast);
}

void ApplyShapeCast3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const ShapeCast3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, ShapeCast3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ShapeCast3DComponentUVE>(
        entity, SanitizeShapeCast3DComponentUVE(value.shapeCast));
}

bool StepShapeCast3DUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<ShapeCast3DComponentUVE>(entity)) {
        return false;
    }
    ShapeCast3DComponentUVE& cast = entityManager.GetComponentUVE<ShapeCast3DComponentUVE>(entity);
    if (!cast.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
        !IsShapeCast3DComponentValidUVE(cast) || cast.collisionMask == 0U ||
        !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        ClearShapeCast3DResultUVE(cast);
        return false;
    }
    const WorldTransformComponentUVE& world =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
    if (!IsWorldTransformComponentValidUVE(world)) {
        ClearShapeCast3DResultUVE(cast);
        return false;
    }
    const Math::Vector3UVE worldDir =
        RayCast3DUVE::ResolveWorldDirectionUVE(cast.direction, world.worldRotation);
    if (Math::LengthSquaredUVE(worldDir) <= 1.0e-8F) {
        ClearShapeCast3DResultUVE(cast);
        return false;
    }

    Math::RayUVE ray{};
    ray.origin = world.worldPosition;
    ray.direction = worldDir;

    switch (cast.shapeKind) {
    case ShapeCast3DShapeKindUVE::Sphere: {
        Physics::SphereCastQueryUVE query{};
        query.ray = ray;
        query.radius = cast.radius;
        query.maxDistance = cast.maxDistance;
        query.layerMask = cast.collisionMask;
        query.ignoreEntity = entity;
        query.alsoIgnoreEntity = cast.alsoIgnoreEntity;
        if (const auto hit = Physics::ShapeCastSystemUVE::SphereCastUVE(entityManager, query);
            hit.has_value()) {
            cast.hit = true;
            cast.hitEntity = hit->entity;
            cast.hitCenter = hit->center;
            cast.hitNormal = hit->normal;
            cast.hitDistance = hit->distance;
            return true;
        }
        break;
    }
    case ShapeCast3DShapeKindUVE::Box: {
        Physics::BoxCastQueryUVE query{};
        query.ray = ray;
        query.halfExtents = cast.halfExtents;
        query.maxDistance = cast.maxDistance;
        query.layerMask = cast.collisionMask;
        query.ignoreEntity = entity;
        if (const auto hit = Physics::ShapeCastSystemUVE::BoxCastUVE(entityManager, query);
            hit.has_value()) {
            cast.hit = true;
            cast.hitEntity = hit->entity;
            cast.hitCenter = hit->center;
            cast.hitNormal = hit->normal;
            cast.hitDistance = hit->distance;
            return true;
        }
        break;
    }
    case ShapeCast3DShapeKindUVE::Capsule: {
        Physics::CapsuleCastQueryUVE query{};
        query.ray = ray;
        query.radius = cast.radius;
        query.height = cast.height;
        query.maxDistance = cast.maxDistance;
        query.layerMask = cast.collisionMask;
        query.ignoreEntity = entity;
        if (const auto hit = Physics::ShapeCastSystemUVE::CapsuleCastUVE(entityManager, query);
            hit.has_value()) {
            cast.hit = true;
            cast.hitEntity = hit->entity;
            cast.hitCenter = hit->center;
            cast.hitNormal = hit->normal;
            cast.hitDistance = hit->distance;
            return true;
        }
        break;
    }
    }
    ClearShapeCast3DResultUVE(cast);
    return false;
}

std::size_t UpdateShapeCast3DObjectsUVE(IEntityManagerUVE& entityManager) {
    std::size_t hitCount = 0U;
    entityManager.ForEachUVE<ShapeCast3DComponentUVE>(
        [&](const EntityUVE entity, ShapeCast3DComponentUVE&) {
            if (StepShapeCast3DUVE(entityManager, entity)) {
                ++hitCount;
            }
        });
    return hitCount;
}

} // namespace UVE::Scene
