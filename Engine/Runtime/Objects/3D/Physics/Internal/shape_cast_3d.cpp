// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "shape_cast_3d.h"

#include <cmath>

#include "disabled_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"
#include "ray_cast_3d.h"
#include "shape_cast_system.h"

namespace UVE::Scene {
namespace {

void ClearShapeCast3DResult(ShapeCast3DComponent& cast) noexcept {
    cast.hit = false;
    cast.hitEntity = kInvalidEntity;
    cast.hitCenter = Math::Vector3{0.0F, 0.0F, 0.0F};
    cast.hitNormal = Math::Vector3{0.0F, 0.0F, 0.0F};
    cast.hitDistance = 0.0F;
}

} // namespace

bool IsShapeCast3DComponentValid(const ShapeCast3DComponent& value) noexcept {
    if (!IsFinite3DObjectVector(value.direction) ||
        Math::LengthSquared(value.direction) <= 1.0e-8F || !std::isfinite(value.maxDistance) ||
        value.maxDistance <= 0.0F) {
        return false;
    }
    switch (value.shapeKind) {
    case ShapeCast3DShapeKind::Sphere:
        return std::isfinite(value.radius) && value.radius > 0.0F;
    case ShapeCast3DShapeKind::Box:
        return IsFinite3DObjectVector(value.halfExtents) && value.halfExtents.x > 0.0F &&
               value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F;
    case ShapeCast3DShapeKind::Capsule:
        return std::isfinite(value.radius) && value.radius > 0.0F && std::isfinite(value.height) &&
               value.height > 0.0F;
    }
    return false;
}

ShapeCast3DComponent SanitizeShapeCast3DComponent(
    const ShapeCast3DComponent& value) noexcept {
    ShapeCast3DComponent out = value;
    if (static_cast<std::uint8_t>(out.shapeKind) >
        static_cast<std::uint8_t>(ShapeCast3DShapeKind::Capsule)) {
        out.shapeKind = ShapeCast3DShapeKind::Sphere;
    }
    if (!IsFinite3DObjectVector(out.direction) ||
        Math::LengthSquared(out.direction) <= 1.0e-8F) {
        out.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    }
    if (!std::isfinite(out.maxDistance) || out.maxDistance <= 0.0F) {
        out.maxDistance = 10.0F;
    }
    if (!std::isfinite(out.radius) || out.radius <= 0.0F) {
        out.radius = 0.5F;
    }
    if (!IsFinite3DObjectVector(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F || out.halfExtents.z <= 0.0F) {
        out.halfExtents = Math::Vector3{0.5F, 0.5F, 0.5F};
    }
    if (!std::isfinite(out.height) || out.height <= 0.0F) {
        out.height = 1.5F;
    }
    return out;
}

bool IsShapeCast3DObjectDefinitionValid(
    const ShapeCast3DObjectDefinition& value) noexcept {
    return IsShapeCast3DComponentValid(value.shapeCast);
}

void ApplyShapeCast3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                         const ShapeCast3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, ShapeCast3DObjectDefinition::defaultName);
    entityManager.AddComponent<ShapeCast3DComponent>(
        entity, SanitizeShapeCast3DComponent(value.shapeCast));
}

bool StepShapeCast3D(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<ShapeCast3DComponent>(entity)) {
        return false;
    }
    ShapeCast3DComponent& cast = entityManager.GetComponent<ShapeCast3DComponent>(entity);
    if (!cast.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
        !IsShapeCast3DComponentValid(cast) || cast.collisionMask == 0U ||
        !entityManager.HasComponent<WorldTransformComponent>(entity)) {
        ClearShapeCast3DResult(cast);
        return false;
    }
    const WorldTransformComponent& world =
        entityManager.GetComponent<WorldTransformComponent>(entity);
    if (!IsWorldTransformComponentValid(world)) {
        ClearShapeCast3DResult(cast);
        return false;
    }
    const Math::Vector3 worldDir =
        RayCast3D::ResolveWorldDirection(cast.direction, world.worldRotation);
    if (Math::LengthSquared(worldDir) <= 1.0e-8F) {
        ClearShapeCast3DResult(cast);
        return false;
    }

    Math::Ray ray{};
    ray.origin = world.worldPosition;
    ray.direction = worldDir;

    switch (cast.shapeKind) {
    case ShapeCast3DShapeKind::Sphere: {
        Physics::SphereCastQuery query{};
        query.ray = ray;
        query.radius = cast.radius;
        query.maxDistance = cast.maxDistance;
        query.layerMask = cast.collisionMask;
        query.ignoreEntity = entity;
        query.alsoIgnoreEntity = cast.alsoIgnoreEntity;
        if (const auto hit = Physics::ShapeCastSystem::SphereCast(entityManager, query);
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
    case ShapeCast3DShapeKind::Box: {
        Physics::BoxCastQuery query{};
        query.ray = ray;
        query.halfExtents = cast.halfExtents;
        query.maxDistance = cast.maxDistance;
        query.layerMask = cast.collisionMask;
        query.ignoreEntity = entity;
        if (const auto hit = Physics::ShapeCastSystem::BoxCast(entityManager, query);
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
    case ShapeCast3DShapeKind::Capsule: {
        Physics::CapsuleCastQuery query{};
        query.ray = ray;
        query.radius = cast.radius;
        query.height = cast.height;
        query.maxDistance = cast.maxDistance;
        query.layerMask = cast.collisionMask;
        query.ignoreEntity = entity;
        if (const auto hit = Physics::ShapeCastSystem::CapsuleCast(entityManager, query);
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
    ClearShapeCast3DResult(cast);
    return false;
}

std::size_t UpdateShapeCast3DObjects(IEntityManager& entityManager) {
    std::size_t hitCount = 0U;
    entityManager.ForEach<ShapeCast3DComponent>(
        [&](const Entity entity, ShapeCast3DComponent&) {
            if (StepShapeCast3D(entityManager, entity)) {
                ++hitCount;
            }
        });
    return hitCount;
}

} // namespace UVE::Scene
