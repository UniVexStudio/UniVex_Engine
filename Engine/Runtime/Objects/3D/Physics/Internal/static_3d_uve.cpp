// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/static_3d_uve.h"

#include "uve/component/rigid_3d_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/collider_3d_uve.h"
#include "uve/objects/3d/object_3d_uve.h"
#include "uve/objects/3d/rigid_3d_uve.h"

namespace UVE::Scene {

bool IsStatic3DObjectDefinitionValidUVE(const Static3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider);
}

void ApplyStatic3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Static3DObjectDefinitionUVE& value) {
    // Static3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, Static3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

bool Static3DUVE::IsImmovableUVE() noexcept {
    return true;
}

float Static3DUVE::InverseMassUVE() noexcept {
    return 0.0F;
}

Math::Vector3UVE Static3DUVE::ResolveZeroVelocityUVE() noexcept {
    return Math::Vector3UVE{0.0F, 0.0F, 0.0F};
}

Math::Vector3UVE Static3DUVE::ResolveZeroAngularVelocityUVE() noexcept {
    return Math::Vector3UVE{0.0F, 0.0F, 0.0F};
}

bool Static3DUVE::IsWorldGeometryUVE(const IEntityManagerUVE& entityManager,
                                     const EntityUVE entity) noexcept {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<ColliderComponentUVE>(entity)) {
        return false;
    }
    if (!Collider3DUVE::IsParticipatingUVE(
            entityManager.GetComponentUVE<ColliderComponentUVE>(entity))) {
        return false;
    }
    if (!entityManager.HasComponentUVE<Rigid3DComponentUVE>(entity)) {
        return true;
    }
    const Rigid3DComponentUVE& body = entityManager.GetComponentUVE<Rigid3DComponentUVE>(entity);
    return !Rigid3DUVE::IsDynamicUVE(body) || Rigid3DUVE::InverseMassUVE(body) <= 0.0F;
}

Math::AabbUVE Static3DUVE::ComputeWorldAabbUVE(
    const ColliderComponentUVE& collider,
    const WorldTransformComponentUVE& worldTransform) noexcept {
    return Collider3DUVE::GetWorldAabbUVE(collider, worldTransform.worldPosition,
                                          worldTransform.worldRotation, worldTransform.worldScale);
}

bool Static3DUVE::ContainsWorldPointUVE(const ColliderComponentUVE& collider,
                                        const WorldTransformComponentUVE& worldTransform,
                                        const Math::Vector3UVE& worldPoint) noexcept {
    return Collider3DUVE::ContainsPointUVE(ScaleColliderUVE(collider, worldTransform.worldScale),
                                           worldTransform.worldPosition, worldTransform.worldRotation,
                                           worldPoint);
}

std::optional<Math::RayHitUVE> Static3DUVE::IntersectWorldRayUVE(
    const ColliderComponentUVE& collider, const WorldTransformComponentUVE& worldTransform,
    const Math::RayUVE& ray, const float maxDistance) noexcept {
    return Collider3DUVE::IntersectRayUVE(ScaleColliderUVE(collider, worldTransform.worldScale),
                                          worldTransform.worldPosition, worldTransform.worldRotation, ray,
                                          maxDistance);
}

} // namespace UVE::Scene
