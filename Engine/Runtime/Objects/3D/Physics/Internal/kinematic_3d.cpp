// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "kinematic_3d.h"

#include <cmath>

#include "i_entity_manager.h"
#include "abstract_physics_objects_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsUsableVelocity(const Math::Vector3& value) noexcept {
    return IsFinite3DObjectVector(value);
}

} // namespace

bool IsKinematic3DObjectComponentValid(const Kinematic3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.targetVelocity) && std::isfinite(value.interpolation) &&
           value.interpolation >= 0.0F && value.interpolation <= 1.0F;
}

bool IsKinematic3DObjectDefinitionValid(const Kinematic3DObjectDefinition& value) noexcept {
    return IsColliderComponentValid(value.collider) && IsRigid3DComponentValid(value.body) &&
           value.body.isKinematic && IsKinematic3DObjectComponentValid(value.animatableBody);
}

void ApplyKinematic3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                            const Kinematic3DObjectDefinition& value) {
    ApplyPhysicsObject3DBase(entityManager, entity, Kinematic3DObjectDefinition::defaultName);
    entityManager.AddComponent<ColliderComponent>(entity, value.collider);
    entityManager.AddComponent<Rigid3DComponent>(entity, value.body);
    entityManager.AddComponent<Kinematic3DComponent>(entity, value.animatableBody);
}

bool Kinematic3D::IsDriving(const Kinematic3DComponent& kinematic) noexcept {
    return kinematic.active;
}

float Kinematic3D::EaseBlend(const float interpolation, const float deltaTimeSeconds) noexcept {
    if (!std::isfinite(interpolation) || interpolation <= 0.0F) {
        return 0.0F;
    }
    if (interpolation >= 1.0F) {
        return 1.0F;
    }
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0.0F;
    }
    return 1.0F - std::pow(1.0F - interpolation, deltaTimeSeconds);
}

Math::Vector3 Kinematic3D::ResolveWorldTarget(const Math::Vector3& localTarget,
                                                       const Math::Quaternion& worldRotation) noexcept {
    if (!IsUsableVelocity(localTarget)) {
        return {};
    }
    Math::Quaternion rotation{};
    if (!Math::TryNormalize(worldRotation, rotation)) {
        rotation = {};
    }
    return Math::RotateVector(rotation, localTarget);
}

Math::Vector3 Kinematic3D::EaseVelocity(const Math::Vector3& currentWorldVelocity,
                                                 const Math::Vector3& worldTarget, const float interpolation,
                                                 const float deltaTimeSeconds) noexcept {
    Math::Vector3 velocity = IsUsableVelocity(currentWorldVelocity) ? currentWorldVelocity : Math::Vector3{};
    const Math::Vector3 target = IsUsableVelocity(worldTarget) ? worldTarget : Math::Vector3{};
    const float blend = EaseBlend(interpolation, deltaTimeSeconds);
    if (blend >= 1.0F) {
        velocity = target;
    } else if (blend > 0.0F) {
        velocity += (target - velocity) * blend;
    }
    return IsUsableVelocity(velocity) ? velocity : Math::Vector3{};
}

} // namespace UVE::Scene
