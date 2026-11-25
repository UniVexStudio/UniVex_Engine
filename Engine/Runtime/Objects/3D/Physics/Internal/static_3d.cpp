// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "static_3d.h"

#include "rigid_3d_component.h"
#include "i_entity_manager.h"
#include "collider_3d.h"
#include "object_3d.h"
#include "rigid_3d.h"

namespace UVE::Scene {

bool IsStatic3DObjectDefinitionValid(const Static3DObjectDefinition& value) noexcept {
    return IsColliderComponentValid(value.collider);
}

void ApplyStatic3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                      const Static3DObjectDefinition& value) {
    // Static3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaseline(entityManager, entity, Static3DObjectDefinition::defaultName);
    entityManager.AddComponent<ColliderComponent>(entity, value.collider);
}

bool Static3D::IsImmovable() noexcept {
    return true;
}

float Static3D::InverseMass() noexcept {
    return 0.0F;
}

Math::Vector3 Static3D::ResolveZeroVelocity() noexcept {
    return Math::Vector3{0.0F, 0.0F, 0.0F};
}

Math::Vector3 Static3D::ResolveZeroAngularVelocity() noexcept {
    return Math::Vector3{0.0F, 0.0F, 0.0F};
}

bool Static3D::IsWorldGeometry(const IEntityManager& entityManager,
                                     const Entity entity) noexcept {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<ColliderComponent>(entity)) {
        return false;
    }
    if (!Collider3D::IsParticipating(
            entityManager.GetComponent<ColliderComponent>(entity))) {
        return false;
    }
    if (!entityManager.HasComponent<Rigid3DComponent>(entity)) {
        return true;
    }
    const Rigid3DComponent& body = entityManager.GetComponent<Rigid3DComponent>(entity);
    return !Rigid3D::IsDynamic(body) || Rigid3D::InverseMass(body) <= 0.0F;
}

Math::Aabb Static3D::ComputeWorldAabb(
    const ColliderComponent& collider,
    const WorldTransformComponent& worldTransform) noexcept {
    return Collider3D::GetWorldAabb(collider, worldTransform.worldPosition,
                                          worldTransform.worldRotation, worldTransform.worldScale);
}

bool Static3D::ContainsWorldPoint(const ColliderComponent& collider,
                                        const WorldTransformComponent& worldTransform,
                                        const Math::Vector3& worldPoint) noexcept {
    return Collider3D::ContainsPoint(ScaleCollider(collider, worldTransform.worldScale),
                                           worldTransform.worldPosition, worldTransform.worldRotation,
                                           worldPoint);
}

std::optional<Math::RayHit> Static3D::IntersectWorldRay(
    const ColliderComponent& collider, const WorldTransformComponent& worldTransform,
    const Math::Ray& ray, const float maxDistance) noexcept {
    return Collider3D::IntersectRay(ScaleCollider(collider, worldTransform.worldScale),
                                          worldTransform.worldPosition, worldTransform.worldRotation, ray,
                                          maxDistance);
}

} // namespace UVE::Scene
